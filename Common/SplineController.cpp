<<<<<<< HEAD
#include "pch.h"
#include "SplineController.h"
#include "d3dApp.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace DirectX::SimpleMath;

namespace
{
    Vector3 CatmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t)
    {
        const float t2 = t * t;
        const float t3 = t2 * t;

        return 0.5f * (
            (2.0f * p1) +
            (-p0 + p2) * t +
            (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
            (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
    }
}

SplineController::SplineController(const std::vector<Vector3>& points, float speed)
    : m_points(points)
    , m_speed(speed)
    , m_currentOffset(0.0f)
    , m_isPlaying(true)
{
    if (m_points.empty()) {
        throw std::runtime_error("SplineController needs at least 1 point");
    }
}

SplineController::SplineController(std::initializer_list<Vector3> points, float speed)
    : SplineController(std::vector<Vector3>(points), speed)
{
}

SplineController::SplineController(const BezierCurve& curve, float speed)
    : SplineController(curve.GetControlPoints(), speed)
{
}

void SplineController::Update()
{
    if (!m_isPlaying) return;

    const float dt = Common::D3DApp::GetApp().GetTimer()->DeltaTime();
    m_currentOffset += m_direction * m_speed * dt;

    if (m_currentOffset > 1.0f) {
        if (m_isPingPong) {
            m_currentOffset = 2.0f - m_currentOffset;
            m_direction = -1;
        }
        else if (m_isLooping) {
            m_currentOffset -= std::floor(m_currentOffset);
        }
        else {
            m_currentOffset = 1.0f;
            m_isPlaying = false;
        }
    }
    else if (m_currentOffset < 0.0f) {
        if (m_isPingPong) {
            m_currentOffset = -m_currentOffset;
            m_direction = 1;
        }
        else if (m_isLooping) {
            m_currentOffset -= std::floor(m_currentOffset);
        }
        else {
            m_currentOffset = 0.0f;
            m_isPlaying = false;
        }
    }

    ApplyCurrentPosition();
}

void SplineController::Play()
{
    m_isPlaying = true;
}

void SplineController::Stop()
{
    m_isPlaying = false;
}

void SplineController::Reset()
{
    m_currentOffset = 0.0f;
    m_direction = 1;
    ApplyCurrentPosition();
}

void SplineController::SetOffset(float t)
{
    m_currentOffset = std::clamp(t, 0.0f, 1.0f);
    ApplyCurrentPosition();
}

float SplineController::GetOffset() const
{
    return m_currentOffset;
}

// getters and setters
void SplineController::SetSpeed(float speed)
{
    m_speed = speed;
}

float SplineController::GetSpeed() const
{
    return m_speed;
}

void SplineController::SetLooping(bool looping)
{
    m_isLooping = looping;
}

bool SplineController::GetLooping() const
{
    return m_isLooping;
}

void SplineController::SetPingPong(bool pingPong)
{
    m_isPingPong = pingPong;
}

bool SplineController::GetPingPong() const
{
    return m_isPingPong;
}

void SplineController::SetPoints(const std::vector<Vector3>& points)
{
    if (points.empty()) {
        throw std::runtime_error("SplineController needs at least 1 point");
    }

    m_points = points;
    m_currentOffset = std::clamp(m_currentOffset, 0.0f, 1.0f);
    m_direction = 1;
    ApplyCurrentPosition();
}

const std::vector<Vector3>& SplineController::GetPoints() const
{
    return m_points;
}

void SplineController::SetCurve(const BezierCurve& curve)
{
    SetPoints(curve.GetControlPoints());
}

Vector3 SplineController::EvaluatePosition(float offset) const
{
    if (m_points.empty()) {
        return Vector3::Zero;
    }

    if (m_points.size() == 1) {
        return m_points[0];
    }

    const float normalizedOffset = std::clamp(offset, 0.0f, 1.0f);

    if (m_points.size() == 2) {
        return Vector3::Lerp(m_points[0], m_points[1], normalizedOffset);
    }

    const size_t segmentCount = m_points.size() - 1;
    const float scaledOffset = normalizedOffset * static_cast<float>(segmentCount);
    const size_t segmentIndex = std::min(static_cast<size_t>(scaledOffset), segmentCount - 1);
    const float localOffset = scaledOffset - static_cast<float>(segmentIndex);

    const size_t p0Index = (segmentIndex == 0) ? 0 : segmentIndex - 1;
    const size_t p1Index = segmentIndex;
    const size_t p2Index = segmentIndex + 1;
    const size_t p3Index = std::min(segmentIndex + 2, m_points.size() - 1);

    return CatmullRom(
        m_points[p0Index],
        m_points[p1Index],
        m_points[p2Index],
        m_points[p3Index],
        localOffset);
}

void SplineController::ApplyCurrentPosition() const
{
    if (gameObject == nullptr || gameObject->GetTransform() == nullptr) {
        return;
    }

    gameObject->GetTransform()->SetPosition(EvaluatePosition(m_currentOffset));
}
=======
﻿#include "pch.h"
#include "SplineController.h"
#include "d3dApp.h"

SplineController::SplineController(const BezierCurve& curve, float speed)
    : m_curve(curve)
      , m_speed(speed)
      , m_currentOffset(0.0f)
      , m_isPlaying(true)
{
}

void SplineController::Update()
{
    if (!m_isPlaying) return;
    if (m_currentOffset > 1.0f)
    {
        if (m_isPingPong)
        {
            m_currentOffset = 2.0f - m_currentOffset;
            m_direction = -1;
        }
        else if (m_isLooping)
        {
            m_currentOffset -= 1.0f;
        }
    }
    else if (m_currentOffset < 0.0f)
    {
        if (m_isPingPong)
        {
            m_currentOffset = -m_currentOffset;
            m_direction = 1;
        }
        else if (m_isLooping)
        {
            m_currentOffset += 1.0f;
        }
    }

    const float dt = Common::D3DApp::GetApp().GetTimer()->DeltaTime();
    m_currentOffset += m_direction * m_speed * dt;

    Vector3 newPosition = m_curve.Evaluate(m_currentOffset);

    this->gameObject->GetTransform()->SetPosition(newPosition);
}

void SplineController::Play()
{
    m_isPlaying = true;
}

void SplineController::Stop()
{
    m_isPlaying = false;
}

void SplineController::Reset()
{
    m_currentOffset = 0.0f;
    m_direction = 1;
}

void SplineController::SetOffset(float t)
{
    m_currentOffset = t;
}

float SplineController::GetOffset() const
{
    return m_currentOffset;
}

// getters and setters
void SplineController::SetSpeed(float speed)
{
    m_speed = speed;
}

float SplineController::GetSpeed() const
{
    return m_speed;
}

void SplineController::SetLooping(bool looping)
{
    m_isLooping = looping;
}

bool SplineController::GetLooping() const
{
    return m_isLooping;
}

void SplineController::SetPingPong(bool pingPong)
{
    m_isPingPong = pingPong;
}

bool SplineController::GetPingPong() const
{
    return m_isPingPong;
}

void SplineController::SetCurve(const BezierCurve& curve)
{
    m_curve = curve;
}

const BezierCurve& SplineController::GetCurve() const
{
    return m_curve;
}
>>>>>>> 4fd0808 (clean up solution)
