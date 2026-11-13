#include <SFML/Graphics.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace
{
constexpr unsigned int kInitialWidth = 960;
constexpr unsigned int kInitialHeight = 640;
constexpr int kInitialBallCount = 18;
constexpr int kPhysicsSubsteps = 8;
constexpr float kMaxFrameDt = 0.05f;
constexpr float kMinRadius = 12.f;
constexpr float kMaxRadius = 34.f;
constexpr float kMinSpeed = 120.f;
constexpr float kMaxSpeed = 280.f;
constexpr float kPi = 3.14159265358979323846f;

struct Ball
{
    sf::Vector2f pos;
    sf::Vector2f vel;
    float radius;
    float mass;
    sf::Color color;
};

float dot(sf::Vector2f a, sf::Vector2f b)
{
    return a.x * b.x + a.y * b.y;
}

float lengthSq(sf::Vector2f v)
{
    return dot(v, v);
}

sf::Color makeVibrantColor(std::mt19937& rng)
{
    // Evenly bright HSV-like RGB palette for clear visual contrast on dark background
    std::uniform_real_distribution<float> hueDist(0.f, 360.f);
    std::uniform_real_distribution<float> satDist(0.65f, 0.95f);
    std::uniform_real_distribution<float> valDist(0.75f, 0.98f);

    const float h = hueDist(rng);
    const float s = satDist(rng);
    const float v = valDist(rng);

    const float c = v * s;
    const float x = c * (1.f - std::abs(std::fmod(h / 60.f, 2.f) - 1.f));
    const float m = v - c;

    float r = 0.f, g = 0.f, b = 0.f;
    if (h < 60.f)      { r = c; g = x; b = 0.f; }
    else if (h < 120.f){ r = x; g = c; b = 0.f; }
    else if (h < 180.f){ r = 0.f; g = c; b = x; }
    else if (h < 240.f){ r = 0.f; g = x; b = c; }
    else if (h < 300.f){ r = x; g = 0.f; b = c; }
    else               { r = c; g = 0.f; b = x; }

    return sf::Color(
        static_cast<std::uint8_t>((r + m) * 255.f),
        static_cast<std::uint8_t>((g + m) * 255.f),
        static_cast<std::uint8_t>((b + m) * 255.f));
}

bool overlapsAny(const std::vector<Ball>& balls, sf::Vector2f pos, float radius)
{
    for (const auto& b : balls)
    {
        const float minDist = b.radius + radius + 2.f;
        if (lengthSq(pos - b.pos) < minDist * minDist)
            return true;
    }
    return false;
}

Ball createBall(sf::Vector2f pos, sf::Vector2f vel, float radius, std::mt19937& rng)
{
    return Ball{pos, vel, radius, radius * radius, makeVibrantColor(rng)};
}

std::vector<Ball> spawnInitialBalls(int count, sf::Vector2u bounds, std::mt19937& rng)
{
    std::vector<Ball> balls;
    balls.reserve(count);

    std::uniform_real_distribution<float> radiusDist(kMinRadius, kMaxRadius);
    std::uniform_real_distribution<float> speedDist(kMinSpeed, kMaxSpeed);
    std::uniform_real_distribution<float> angleDist(0.f, 2.f * kPi);

    for (int i = 0; i < count; ++i)
    {
        const float r = radiusDist(rng);
        if (bounds.x <= 2.f * r || bounds.y <= 2.f * r)
            continue;

        std::uniform_real_distribution<float> xDist(r + 2.f, static_cast<float>(bounds.x) - r - 2.f);
        std::uniform_real_distribution<float> yDist(r + 2.f, static_cast<float>(bounds.y) - r - 2.f);

        sf::Vector2f pos;
        bool placed = false;
        for (int attempt = 0; attempt < 200; ++attempt)
        {
            pos = {xDist(rng), yDist(rng)};
            if (!overlapsAny(balls, pos, r))
            {
                placed = true;
                break;
            }
        }
        if (!placed)
            continue;

        const float speed = speedDist(rng);
        const float angle = angleDist(rng);
        const sf::Vector2f vel{speed * std::cos(angle), speed * std::sin(angle)};
        balls.push_back(createBall(pos, vel, r, rng));
    }

    return balls;
}

void resolveWallCollisions(Ball& ball, sf::Vector2f bounds)
{
    if (ball.pos.x - ball.radius < 0.f)
    {
        ball.pos.x = ball.radius;
        ball.vel.x = std::abs(ball.vel.x);
    }
    else if (ball.pos.x + ball.radius > bounds.x)
    {
        ball.pos.x = std::max(ball.radius, bounds.x - ball.radius);
        ball.vel.x = -std::abs(ball.vel.x);
    }

    if (ball.pos.y - ball.radius < 0.f)
    {
        ball.pos.y = ball.radius;
        ball.vel.y = std::abs(ball.vel.y);
    }
    else if (ball.pos.y + ball.radius > bounds.y)
    {
        ball.pos.y = std::max(ball.radius, bounds.y - ball.radius);
        ball.vel.y = -std::abs(ball.vel.y);
    }
}

void resolveBallCollision(Ball& a, Ball& b)
{
    const sf::Vector2f delta = b.pos - a.pos;
    const float minDist = a.radius + b.radius;
    const float distSq = lengthSq(delta);

    if (distSq >= minDist * minDist)
        return;

    const float dist = std::sqrt(distSq);
    const sf::Vector2f normal = (dist > 1e-6f) ? (delta / dist) : sf::Vector2f{1.f, 0.f};

    const float invMassA = 1.f / a.mass;
    const float invMassB = 1.f / b.mass;
    const float invMassSum = invMassA + invMassB;

    // Positional correction so overlapping balls do not sink into one another
    const float penetration = minDist - dist;
    const sf::Vector2f correction = normal * (penetration / invMassSum);
    a.pos -= correction * invMassA;
    b.pos += correction * invMassB;

    // Perfectly elastic impulse (restitution e = 1.0) along the collision normal
    const sf::Vector2f relVel = b.vel - a.vel;
    const float velAlongNormal = dot(relVel, normal);
    if (velAlongNormal >= 0.f)
        return;

    const float impulseMag = (-2.f * velAlongNormal) / invMassSum;
    const sf::Vector2f impulse = normal * impulseMag;
    a.vel -= impulse * invMassA;
    b.vel += impulse * invMassB;
}

void stepPhysics(std::vector<Ball>& balls, float dt, sf::Vector2f bounds)
{
    const float subDt = dt / static_cast<float>(kPhysicsSubsteps);
    for (int step = 0; step < kPhysicsSubsteps; ++step)
    {
        for (auto& ball : balls)
        {
            ball.pos += ball.vel * subDt;
            resolveWallCollisions(ball, bounds);
        }

        const std::size_t n = balls.size();
        for (std::size_t i = 0; i < n; ++i)
        {
            for (std::size_t j = i + 1; j < n; ++j)
                resolveBallCollision(balls[i], balls[j]);
        }

        // Re-clamp against borders in case ball-ball separation pushed a ball slightly outside
        for (auto& ball : balls)
            resolveWallCollisions(ball, bounds);
    }
}

double totalKineticEnergy(const std::vector<Ball>& balls)
{
    double ke = 0.0;
    for (const auto& b : balls)
        ke += 0.5 * static_cast<double>(b.mass) * static_cast<double>(lengthSq(b.vel));
    return ke;
}

std::string buildWindowTitle(const std::vector<Ball>& balls, bool paused)
{
    std::ostringstream oss;
    oss << "Ball Physics (Zero-G)"
        << " | Balls: " << balls.size()
        << " | KE: " << std::fixed << std::setprecision(0) << (totalKineticEnergy(balls) / 1e6) << "M";
    if (paused)
        oss << " | PAUSED";
    oss << " | LMB: Spawn/Launch  RMB: Remove  Space: Pause  R: Reset  C: Clear";
    return oss.str();
}
} // namespace

int main()
{
    std::random_device rd;
    std::mt19937 rng(rd());

    sf::ContextSettings settings;
    settings.antiAliasingLevel = 4;

    sf::RenderWindow window(
        sf::VideoMode({kInitialWidth, kInitialHeight}),
        "Ball Physics (Zero-G)",
        sf::Style::Default,
        sf::State::Windowed,
        settings);
    window.setVerticalSyncEnabled(true);

    std::vector<Ball> balls = spawnInitialBalls(kInitialBallCount, window.getSize(), rng);
    bool paused = false;

    bool dragging = false;
    sf::Vector2f dragStart{};
    sf::Vector2f dragCurrent{};
    float previewRadius = 22.f;

    sf::Clock frameClock;
    sf::Clock titleClock;
    window.setTitle(buildWindowTitle(balls, paused));

    while (window.isOpen())
    {
        const float dt = std::min(frameClock.restart().asSeconds(), kMaxFrameDt);

        while (const std::optional event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                window.close();
            }
            else if (const auto* resized = event->getIf<sf::Event::Resized>())
            {
                const sf::FloatRect visibleArea({0.f, 0.f}, {static_cast<float>(resized->size.x), static_cast<float>(resized->size.y)});
                window.setView(sf::View(visibleArea));
                const sf::Vector2f bounds{static_cast<float>(resized->size.x), static_cast<float>(resized->size.y)};
                for (auto& ball : balls)
                    resolveWallCollisions(ball, bounds);
            }
            else if (const auto* key = event->getIf<sf::Event::KeyPressed>())
            {
                if (key->code == sf::Keyboard::Key::Space)
                {
                    paused = !paused;
                    window.setTitle(buildWindowTitle(balls, paused));
                }
                else if (key->code == sf::Keyboard::Key::R)
                {
                    balls = spawnInitialBalls(kInitialBallCount, window.getSize(), rng);
                    window.setTitle(buildWindowTitle(balls, paused));
                }
                else if (key->code == sf::Keyboard::Key::C)
                {
                    balls.clear();
                    window.setTitle(buildWindowTitle(balls, paused));
                }
                else if (key->code == sf::Keyboard::Key::Escape)
                {
                    window.close();
                }
            }
            else if (const auto* mouse = event->getIf<sf::Event::MouseButtonPressed>())
            {
                const sf::Vector2f clickPos{static_cast<float>(mouse->position.x), static_cast<float>(mouse->position.y)};
                if (mouse->button == sf::Mouse::Button::Left)
                {
                    std::uniform_real_distribution<float> radiusDist(kMinRadius, kMaxRadius);
                    previewRadius = radiusDist(rng);
                    dragging = true;
                    dragStart = clickPos;
                    dragCurrent = clickPos;
                }
                else if (mouse->button == sf::Mouse::Button::Right)
                {
                    for (auto it = balls.rbegin(); it != balls.rend(); ++it)
                    {
                        if (lengthSq(clickPos - it->pos) <= it->radius * it->radius)
                        {
                            balls.erase(std::next(it).base());
                            break;
                        }
                    }
                }
            }
            else if (const auto* moved = event->getIf<sf::Event::MouseMoved>())
            {
                if (dragging)
                    dragCurrent = {static_cast<float>(moved->position.x), static_cast<float>(moved->position.y)};
            }
            else if (const auto* released = event->getIf<sf::Event::MouseButtonReleased>())
            {
                if (dragging && released->button == sf::Mouse::Button::Left)
                {
                    dragging = false;
                    const sf::Vector2f bounds{static_cast<float>(window.getSize().x), static_cast<float>(window.getSize().y)};

                    sf::Vector2f spawnPos = dragStart;
                    spawnPos.x = std::clamp(spawnPos.x, previewRadius, std::max(previewRadius, bounds.x - previewRadius));
                    spawnPos.y = std::clamp(spawnPos.y, previewRadius, std::max(previewRadius, bounds.y - previewRadius));

                    const sf::Vector2f dragDelta = dragCurrent - dragStart;
                    sf::Vector2f vel;
                    if (lengthSq(dragDelta) > 25.f)
                    {
                        // Slingshot-style launch velocity proportional to drag vector
                        vel = dragDelta * 2.5f;
                    }
                    else
                    {
                        std::uniform_real_distribution<float> speedDist(kMinSpeed, kMaxSpeed);
                        std::uniform_real_distribution<float> angleDist(0.f, 2.f * kPi);
                        const float speed = speedDist(rng);
                        const float angle = angleDist(rng);
                        vel = {speed * std::cos(angle), speed * std::sin(angle)};
                    }

                    balls.push_back(createBall(spawnPos, vel, previewRadius, rng));
                    window.setTitle(buildWindowTitle(balls, paused));
                }
            }
        }

        if (!paused)
        {
            const sf::Vector2f bounds{static_cast<float>(window.getSize().x), static_cast<float>(window.getSize().y)};
            stepPhysics(balls, dt, bounds);
        }

        if (titleClock.getElapsedTime().asSeconds() >= 0.25f)
        {
            titleClock.restart();
            window.setTitle(buildWindowTitle(balls, paused));
        }

        window.clear(sf::Color(18, 20, 28));

        sf::CircleShape shape;
        shape.setOutlineThickness(-1.5f);
        shape.setOutlineColor(sf::Color(255, 255, 255, 70));

        for (const auto& ball : balls)
        {
            shape.setRadius(ball.radius);
            shape.setPointCount(40);
            shape.setOrigin({ball.radius, ball.radius});
            shape.setPosition(ball.pos);
            shape.setFillColor(ball.color);
            window.draw(shape);
        }

        if (dragging)
        {
            sf::CircleShape ghost(previewRadius, 40);
            ghost.setOrigin({previewRadius, previewRadius});
            ghost.setPosition(dragStart);
            ghost.setFillColor(sf::Color(255, 255, 255, 45));
            ghost.setOutlineThickness(1.5f);
            ghost.setOutlineColor(sf::Color(255, 255, 255, 180));
            window.draw(ghost);

            sf::VertexArray aimLine(sf::PrimitiveType::Lines, 2);
            aimLine[0] = sf::Vertex{dragStart, sf::Color(255, 220, 100, 220)};
            aimLine[1] = sf::Vertex{dragCurrent, sf::Color(255, 120, 80, 220)};
            window.draw(aimLine);
        }

        window.display();
    }

    return 0;
}

