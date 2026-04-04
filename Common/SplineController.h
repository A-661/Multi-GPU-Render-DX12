#pragma once
#include "Component.h"
#include "BezierCurve.h"
#include "GameObject.h"
#include "Transform.h"
#include <initializer_list>
#include <vector>

class SplineController : public Component {
public:
    /*
    Example:
    std::vector<Vector3> points = {
    Vector3(-100, 100, -100),
    Vector3(-100, 100, 100),
    Vector3(100, 100, 100),
    Vector3(100, 100, -100)
    };
    auto controller = std::make_shared<SplineController>(points, 0.5f);*/
    SplineController(const std::vector<DirectX::SimpleMath::Vector3>& points, float speed = 1.0f);
    SplineController(std::initializer_list<DirectX::SimpleMath::Vector3> points, float speed = 1.0f);
    SplineController(const BezierCurve& curve, float speed = 1.0f);

    void Update() override;

    void Play();
    void Stop();
    void Reset();

    void SetOffset(float t);
    float GetOffset() const;

    void SetSpeed(float speed);
    float GetSpeed() const;

    void SetLooping(bool looping);
    bool GetLooping() const;

    void SetPingPong(bool pingPong);
    bool GetPingPong() const;

    void SetPoints(const std::vector<DirectX::SimpleMath::Vector3>& points);
    const std::vector<DirectX::SimpleMath::Vector3>& GetPoints() const;

    void SetCurve(const BezierCurve& curve);

private:
    DirectX::SimpleMath::Vector3 EvaluatePosition(float offset) const;
    void ApplyCurrentPosition() const;

    std::vector<DirectX::SimpleMath::Vector3> m_points;
    float m_speed;
    float m_currentOffset;
    bool m_isPlaying; // default true in constructor

    bool m_isLooping = true;
    bool m_isPingPong = false;
    int m_direction = 1;
};

