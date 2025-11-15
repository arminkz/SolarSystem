#pragma once
#include "stdafx.h"


struct CameraParams
{
    float radius = 16.0f;
    float minRadius = 0.1f;  
    float maxRadius = 1000.0f; 

    glm::vec3 target = glm::vec3(0.);

    glm::vec3 initialUp = glm::vec3(0., 1., 0.); // Up vector
    glm::vec3 initialForward = glm::vec3(0., 0., -1.); // Forward vector
};


class Camera 
{
public:

    explicit Camera(const CameraParams& params = CameraParams());

    void changeZoom(float delta);
    void rotateHorizontally(float delta);
    void rotateVertically(float delta);

    // Calculated properties
    glm::mat4 getViewMatrix();
    glm::vec3 getPosition();

    // Raw properties
    float getRadius() const { return _radius; }
    void setRadius(float radius) { _radius = glm::clamp(radius, _minRadius, _maxRadius); };

    glm::vec3 getTarget() const { return _target; }
    void setTarget(const glm::vec3& target);
    void setTargetAnimated(const glm::vec3& target);

    glm::vec3 getForward() const { return _forward; }
    void setForward(const glm::vec3& forward) { _forward = glm::normalize(forward); };

    glm::vec3 getLeft() const { return _left; }
    void setLeft(const glm::vec3& left) { _left = glm::normalize(left); };

    glm::vec3 getUp() const { return _up; }
    void setUp(const glm::vec3& up) { _up = glm::normalize(up); };

    void advanceAnimation(float deltaTime);

private:
    float _radius;
    float _minRadius;
    float _maxRadius;

    glm::vec3 _target;
    glm::vec3 _forward;
    glm::vec3 _left;
    glm::vec3 _up;

    glm::mat4 _viewMatrix;

    // Animation state
    bool _isAnimating = false;
    glm::vec3 _animationStartTarget;
    glm::vec3 _animationEndTarget;
    float _animationDuration = 1.0f; // in seconds
    float _animationElapsed = 0.0f;

    float easeInOutCubic(float t);
};