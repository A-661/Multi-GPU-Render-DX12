#pragma once
#include "Component.h"


class Mousepad;
class KeyboardDevice;

using namespace DirectX::SimpleMath;

class CameraController :
    public Component
{
    KeyboardDevice* keyboard;
    Mousepad* mouse;


    float xMouseSpeed = 100;
    float yMouseSpeed = 70;
    float moveSpeedMultiplier = 1.0f;

public:
    CameraController();

    void SetMoveSpeedMultiplier(float multiplier);
    void Update() override;
};
