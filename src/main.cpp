#include <SFML/Window.hpp>
#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>
#include <SFML/System.hpp>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <ctime>
#include <cstdlib>
#include <JsonBox.h>
#include <algorithm>

#include "constants.hpp"
#include "game_state.hpp"
#include "game_state_title.hpp"
#include "game_state_game.hpp"
#include "entity_manager.hpp"
#include "network_manager.hpp"
#include "game_container.hpp"

class Tileset;
class GameMap;
class Character;

// Easiest way to ensure a otherwise infinite looping function
// will halt is to use a pointer as a kill-switch
void pollNetworkEvents(NetworkManager* nmgr, bool* kill)
{
    // Run until killed, accepting connections and parsing them
    // as network events. The events are available from nmgr
    // using pollEvent, which should be called in the main thread
    while(!*kill)
    {
        nmgr->waitEvent();
    }
}

// Get the key for connected clients from a client charId and gameId
// These are used in NetEvents to uniquely identify each client so as
// to not broadcast ip and port to other clients. It also allows
// multiple connections from the same ip
sf::Uint32 clientKey(const sf::Uint16 gameId, const sf::Uint8 charId)
{
    return sf::Uint32(gameId << 8) + sf::Uint32(charId);
}

int main(int argc, char* argv[])
{
    // Check for a server or a client
    // Should really make two different executables, but
    // there's not really enough time for that
    // TODO: Implement arguments instead of this
    if(argc > 1)
    {
        ld::isServer = true;
    }

    std::srand(std::time(nullptr));

    EntityManager entityManager;
    // Load entities
    entityManager.load<Tileset>("tilesets.json");
    entityManager.load<GameMap>("game_map.json");
    entityManager.load<Character>("characters.json");

    // Load network manager
    JsonBox::Value configFile;
    configFile.loadFromFile("config.json");
    NetworkManager networkManager(configFile[ld::isServer ? "server" : "client"]);

    // Open a thread for listening to incoming connections
    bool killPollNetworkThread = false;
    std::thread pollNetworkThread(pollNetworkEvents, &networkManager, &killPollNetworkThread);

    //////////////////////////////////////////////////////////////////
    // SERVER
    //////////////////////////////////////////////////////////////////
    if(ld::isServer)
    {
        bool running = true;
        // Currently connected clients. Indexed by a hash of their
        // gameId and charId (see clientKey)
        struct ClientInfo {
            sf::IpAddress ip;
            sf::Uint16 port;
            sf::Uint16 gameId;
            sf::Uint8 charId;
        };
        std::map<sf::Uint32, ClientInfo> connectedClients;
        // Games currently running on this server
        std::map<sf::Uint16, GameContainer> games;

        // Game time
        sf::Clock clock;

        while(running)
        {
            float dt = clock.restart().asSeconds();

            NetworkManager::Event netEvent;
            while(networkManager.pollEvent(netEvent))
            {
                switch(netEvent.type)
                {
                    default:
                    case NetworkManager::Event::Nop:
                    {
                        break;
                    }
                    ///////////////////////////////////////////////////
                    // CONNECT
                    ///////////////////////////////////////////////////
                    case NetworkManager::Event::Connect:
                    {
                        auto& e = netEvent.connect;
                        if(std::find_if(connectedClients.begin(), connectedClients.end(),
                            [&e](const std::pair<sf::Uint32, ClientInfo>& a)
                            {
                                return a.second.ip == e.ip && a.second.port == e.port;
                            }) != connectedClients.end())
                        {
                            servout << e.ip.toString() << ":" << e.port << " is already connected" << std::endl;
                        }
                        else
                        {
                            // If the game doesn't exist yet, make it
                            if(games.count(e.gameId) == 0)
                            {
                                games[e.gameId] = GameContainer(entityManager.getEntity<GameMap>("gamemap_5v5"), e.gameId, 255);
                            }
                            GameContainer& game = games[e.gameId];
                            // Attempt to add to a team
                            // TODO: Add team choosing
                            sf::Uint8 charId = 255;
                            if(game.add("character_fighter", GameContainer::Team::Any, &entityManager, &charId))
                            {
                                auto ck = clientKey(e.gameId, charId);
                                // Added to the team, send a success
                                // message to connected clients
                                servout << e.ip.toString() << ":" << e.port << " has connected to game "
                                    << e.gameId << " as character " << (sf::Uint16)charId << std::endl;
                                // Add to the list of connected clients
                                connectedClients[ck] = (ClientInfo){
                                    .ip = e.ip,
                                    .port = e.port,
                                    .gameId = e.gameId,
                                    .charId = charId
                                };
                                // Send an accept to the client who tried
                                // to connect
                                e.team = game.characters[charId].team;
                                e.charId = charId;
                                networkManager.send(netEvent, e.ip, e.port);
                                // Tell the connecting client about existing
                                // clients in the same game
                                for(auto c : connectedClients)
                                {
                                    if(c.second.gameId != connectedClients[ck].gameId || c.first == ck) continue;
                                    // Shorthand reference to character
                                    auto ch = games[e.gameId].characters[c.second.charId];
                                    NetworkManager::Event response;
                                    // Connection information
                                    response.connect = {
                                        .ip = sf::IpAddress(0, 0, 0, 0),
                                        .port = 0,
                                        .gameId = c.second.gameId,
                                        .charId = c.second.charId,
                                        .team = ch.team
                                    };
                                    response.type = NetworkManager::Event::Connect;
                                    networkManager.send(response, e.ip, e.port);
                                    // Position information
                                    response.move = {
                                        .gameId = c.second.gameId,
                                        .charId = c.second.charId,
                                        .target = ch.c.pfHelper.target,
                                        .pos = ch.c.pfHelper.pos
                                    };
                                    response.type = NetworkManager::Event::Move;
                                    networkManager.send(response, e.ip, e.port);
                                }
                                // Send to other clients who are in the same game
                                // Ip and port are not needed by other
                                // clients, and so are masked
                                e.ip = sf::IpAddress(0, 0, 0, 0);
                                e.port = 0;
                                for(auto c : connectedClients)
                                {
                                    // Don't send it to client who requested
                                    // or clients who are not in the same game
                                    if(c.second.gameId != connectedClients[ck].gameId || c.first == ck) continue;
                                    networkManager.send(netEvent, c.second.ip, c.second.port);
                                    servout << "\tNotified " << c.second.ip.toString() << ":" << c.second.port << std::endl;
                                }
                            }
                            else
                            {
                                // Couldn't add, so respond with
                                // a failure message
                                // TODO: When you're 8 indents in,
                                // it's probably time to refactor
                                NetworkManager::Event response;
                                response.gameFull = {
                                    .gameId = e.gameId
                                };
                                response.type = NetworkManager::Event::GameFull;
                                servout << "Game " << e.gameId
                                    << " is full, rejecting with message" << std::endl;
                                networkManager.send(response, e.ip, e.port);
                            }
                        }
                        break;
                    }
                    ///////////////////////////////////////////////////
                    // DISCONNECT
                    ///////////////////////////////////////////////////
                    case NetworkManager::Event::Disconnect:
                    {
                        // Terribly unsecure way of killing server gracefully
                        if(netEvent.disconnect.gameId == 65535 &&
                            netEvent.disconnect.charId == 255)
                        {
                            running = false;
                        }
                        // Same as a connect event but in reverse
                        auto& e = netEvent.disconnect;
                        auto ck = clientKey(e.gameId, e.charId);
                        if(connectedClients.count(ck) == 0)
                        {
                            servout << e.ip.toString() << " is not connected" << std::endl;
                        }
                        // TODO: This section should also be triggered if a client has not been
                        // heard from for a certain amount of time
                        // Double check that the information given by the
                        // client is correct
                        else if(connectedClients[ck].gameId == e.gameId &&
                            connectedClients[ck].charId == e.charId &&
                            connectedClients[ck].ip == e.ip &&
                            connectedClients[ck].port == e.port)
                        {
                            servout << e.ip.toString() << " has disconnected" << std::endl;
                            // Delete from the set of connected clients
                            connectedClients.erase(ck);
                            // Broacast
                            e.ip = sf::IpAddress(0, 0, 0, 0);
                            e.port = 0;
                            for(auto c : connectedClients)
                            {
                                if(c.second.gameId != e.gameId) continue;
                                networkManager.send(netEvent, c.second.ip, c.second.port);
                            }
                        }

                        break;
                    }
                    ///////////////////////////////////////////////////
                    // MOVE
                    ///////////////////////////////////////////////////
                    case NetworkManager::Event::Move:
                    {
                        auto& e = netEvent.move;
                        auto ck = clientKey(e.gameId, e.charId);

                        auto& ch = games[e.gameId].characters[e.charId].c;
                        // If the client position is slightly different to
                        // server position, accept the client as truth
                        // If it's wildly different, accept the server
                        bool changeClient = false;
                        if(vecmath::norm(ch.pfHelper.pos-e.pos) < 0.1) ch.pfHelper.pos = e.pos;
                        else
                        {
                            changeClient = true;
                            e.pos = ch.pfHelper.pos;
                        }
                        // Change the target
                        ch.pfHelper.setTarget(e.target);

                        // Now that corrections have been made, broadcast
                        // to all clients in the game, but only broadcast
                        // to the sender if they had their position changed
                        for(auto c : connectedClients)
                        {
                            if(c.second.gameId != e.gameId ||
                                (changeClient == false && c.first == ck)) continue;
                            networkManager.send(netEvent, c.second.ip, c.second.port);
                        }
                        servout << ck << " sent a move event" << std::endl;
                        break;
                    }
                    ///////////////////////////////////////////////////
                    // DAMAGE
                    ///////////////////////////////////////////////////
                    // TODO
                    case NetworkManager::Event::Damage:
                    {
                        auto& e = netEvent.move;
                        auto ck = clientKey(e.gameId, e.charId);
                        break;
                    }
                    ///////////////////////////////////////////////////
                    // AUTO ATTACK
                    ///////////////////////////////////////////////////
                    // TODO
                    case NetworkManager::Event::AutoAttack:
                    {
                        auto& e = netEvent.autoAttack;
                        auto ck = clientKey(e.gameId, e.charId);
                        break;
                    }
                }
            }
            for(auto& g : games)
            {
                // Process parts of the game which result in events being sent
                for(auto attack : g.second.targetAttacks)
                {
                    if(attack->update(dt))
                    {
                        // Attack has triggered, so tell the clients about it
                        NetworkManager::Event netEvent = attack->getEvent();
                        for(auto c : connectedClients)
                        {
                            if(c.second.gameId != g.first) continue;
                            networkManager.send(netEvent, c.second.ip, c.second.port);
                        }
                    }
                }
                // Process the gameplay
                g.second.update(dt);
            }
        }
    }
    //////////////////////////////////////////////////////////////////
    // CLIENT
    //////////////////////////////////////////////////////////////////
    else
    {
        // Create the window
        sf::RenderWindow window(sf::VideoMode(ld::width, ld::height), ld::title,
                sf::Style::Titlebar | sf::Style::Close);

        // Disable key repeating
        window.setKeyRepeatEnabled(false);
        
        // Clamp framerate
        window.setVerticalSyncEnabled(true);

        // Window viewport
        sf::View view(sf::FloatRect(0, 0, ld::width, ld::height));
        view.setCenter(ld::width / 2.0f, ld::height / 2.0f);
        window.setView(view);

        // Pointer to current game state
        std::shared_ptr<GameState> state;

        // Load initial game state
        state.reset(new GameStateTitle(state, state, &entityManager));

        // Frame time
        sf::Clock clock;

        // Game the client is playing in
        std::shared_ptr<GameContainer> game;

        // Attempt to connect to server
        // TODO: Make this more robust
        auto cts_client = configFile["client"].getObject();
        auto cts_target = cts_client["target"].getObject();
        if(!networkManager.connectToServer(
            cts_target["address"].getString(),
            cts_target["port"].getInteger(),
            cts_target["game"].getInteger()))
        {
            window.close();
        }
        bool hasConnectedToServer = false;

        // Game loop
        while(window.isOpen())
        {
            float dt = clock.restart().asSeconds();

            // Handle events
            sf::Event event;
            while(window.pollEvent(event))
            {
                if(event.type == sf::Event::Closed)
                {
                    window.close();
                }
                if(state != nullptr) state->handleEvent(event, window);
            }
            // Handle network events
            NetworkManager::Event netEvent;
            while(networkManager.pollEvent(netEvent))
            {
                switch(netEvent.type)
                {
                    default:
                    case NetworkManager::Event::Nop:
                    {
                        break;
                    }
                    ///////////////////////////////////////////////////
                    // CONNECT
                    ///////////////////////////////////////////////////
                    case NetworkManager::Event::Connect:
                    {
                        auto e = netEvent.connect;
                        clntout << "A client has connected to game " << e.gameId
                                << " as character " << (sf::Uint16)e.charId << std::endl;
                        // If client has not yet connected and this is addressed
                        // to the client
                        if(!hasConnectedToServer && e.ip == networkManager.getIp())
                        {
                            hasConnectedToServer = true;
                            clntout << "\tIt was me, changing game state" << std::endl;
                            clntout << "\tAdded to team " << static_cast<int>(e.team) << std::endl;
                            // Make a new GameContainer
                            game = std::shared_ptr<GameContainer>(new GameContainer(
                                    entityManager.getEntity<GameMap>("gamemap_5v5"),
                                    e.gameId, e.charId));
                            // Add the client to the game, with the
                            // position and team as given by the server
                            game->add("character_fighter", e.team, &entityManager, &e.charId);
                            // TODO: Add other players
                            state.reset(new GameStateGame(state, state, game, &entityManager, &networkManager));
                        }
                        // Otherwise if this concerns the game the client
                        // is connected to
                        else if(hasConnectedToServer && e.gameId == game->gameId)
                        {
                            // Server says a new player has joined the game
                            clntout << "\tConnected to my game on team " << static_cast<int>(e.team) << std::endl;
                            game->add("character_fighter", e.team, &entityManager, &e.charId);
                        }
                        break;
                    }
                    ///////////////////////////////////////////////////
                    // DISCONNECT
                    ///////////////////////////////////////////////////
                    case NetworkManager::Event::Disconnect:
                    {
                        auto e = netEvent.disconnect;
                        clntout << e.ip.toString() << " has disconnected" << std::endl;
                        break;
                    }
                    ///////////////////////////////////////////////////
                    // GAME FULL
                    ///////////////////////////////////////////////////
                    case NetworkManager::Event::GameFull:
                    {
                        auto e = netEvent.gameFull;
                        clntout << e.gameId << " is full, sorry" << std::endl;
                        window.close();
                        break;
                    }
                    ///////////////////////////////////////////////////
                    // MOVE
                    ///////////////////////////////////////////////////
                    case NetworkManager::Event::Move:
                    {
                        auto e = netEvent.move;
                        if(game == nullptr || e.gameId != game->gameId) break;
                        // Accept position and target changes from the server
                        auto& ch = game->characters[e.charId].c;
                        ch.pfHelper.pos = e.pos;
                        ch.pfHelper.setTarget(e.target);
                        break;
                    }
                    ///////////////////////////////////////////////////
                    // DAMAGE
                    ///////////////////////////////////////////////////
                    case NetworkManager::Event::Damage:
                    {
                        break;
                    }
                    ///////////////////////////////////////////////////
                    // AUTOATTACK
                    ///////////////////////////////////////////////////
                    case NetworkManager::Event::AutoAttack:
                    {
                        auto e = netEvent.autoAttack;
                        if(game == nullptr || e.gameId != game->gameId) break;
                        // This is bad. Hideously, grotesquely bad.
                        // game->targetAttacks.push_back(new TargetAttack(
                        //     e.charId, // Attacker
                        //     e.targetId, // Target
                        //     game->map->tileset, // Tileset of attack anim
                        //     "weapon_fighter_attack", // Anim name
                        //     2,  // Trigger at end of anim,
                        //     [](GameContainer::CharWrapper* source, GameContainer::CharWrapper* target) {
                        //         // Deal damage to the target
                        //         return (NetworkManager::Event){
                        //             .type = NetworkManager::Event::Damage,
                        //             .damage = {
                        //                 // TODO: Actually implement a proper damage function
                        //                 .hp = target->c.hp - source->c.stats.str * 10
                        //             }
                        //         };
                        //     },
                        //     game // Pointer to game the clients are in
                        // ));
                        break;
                    }
                }
            }

            if(state != nullptr)
            {
                // Update window
                state->handleInput(dt, window);
                state->update(dt);
                // Draw window
                window.clear(sf::Color::Black);
                window.draw(*state);
                window.display();
            }
            else
            {
                window.close();
            }
        }

        // Attempt to disconnect from server
        if(game != nullptr)
        {
            networkManager.disconnectFromServer(game->gameId, game->client);
        }
    }

    // Kill the network thread and join
    killPollNetworkThread = true;
    pollNetworkThread.join();

    return 0;
}
