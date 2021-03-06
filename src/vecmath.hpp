#ifndef VECMATH_HPP
#define VECMATH_HPP

#include <SFML/System.hpp>
#include <cmath>

namespace vecmath
{
const double pi = 3.1415926535897;

template<typename T>
T norm(const sf::Vector2<T>& a)
{
    return std::sqrt(a.x*a.x + a.y*a.y);
}

template<typename T>
T manhattan(const sf::Vector2<T>& a)
{
    return std::abs(a.x) + std::abs(a.y);
}

// TODO: Proper conversion operators
template<typename T, typename S>
sf::Vector2<T> to(const sf::Vector2<S>& a)
{
    return sf::Vector2<T>(static_cast<T>(a.x), static_cast<T>(a.y));
}

template<typename T>
T dot(const sf::Vector2<T>& a, const sf::Vector2<T>& b)
{
    return a.x*b.x + a.y*b.y;
}

// Calculate the angle of a to the horizontal, in the range
// [0, 2pi]
template<typename T>
T angle(const sf::Vector2<T>& a)
{
    T theta = std::atan2(a.y, a.x);
    if(theta < 0) theta += 2*pi;
    return theta;
}

template<typename T>
bool intersect(const sf::Vector2<T>& p0, const sf::Vector2<T>& p1,
        const sf::Vector2<T>& q0, const sf::Vector2<T>& q1,
        sf::Vector2<T>* intersection, bool unboundAbove=false)
{
    auto dp = p1 - p0;
    auto dpt = sf::Vector2<T>(-dp.y, dp.x);
    auto dq = q1 - q0;
    auto dqt = sf::Vector2<T>(-dq.y, dq.x);
    // dpt is orthogonal to p1-p0, so if dpt.dq = 0 then
    // dp is parallel to dq and there's no intersection
    if(std::abs(vecmath::dot(dpt, dq)) < 10e-5) return false;
    T mu1 = vecmath::dot((p0 - q0), dpt) / vecmath::dot(dpt, dq);
    T mu2 = vecmath::dot((q0 - p0), dqt) / vecmath::dot(dqt, dp);
    // mu is parameter for Q
    if(0 <= mu1 && 0 <= mu2 && (unboundAbove || mu2 <= 1) && mu1 <= 1)
    {
        intersection->x = q0.x + mu1 * (q1.x-q0.x);
        intersection->y = q0.y + mu1 * (q1.y-q0.y);
        return true;
    }
    else
    {
        return false;
    }
}

// This has no mathematical significance, but it's needed for std::set
template<typename T>
class vec2_compare
{
public:
    bool operator() (const sf::Vector2<T>& lhs, const sf::Vector2<T>& rhs) const
    {
        return (lhs.x < rhs.x || (lhs.x == lhs.x && lhs.y < rhs.y));
    }
};
}

#endif /* VECMATH_HPP */
