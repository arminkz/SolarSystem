#include "Camera.h"

Camera::Camera(const CameraParams& params) {
    _radius = params.radius;
    _minRadius = params.minRadius;
    _maxRadius = params.maxRadius;
    _target = params.target;

    _up = glm::normalize(params.initialUp);
    _forward = glm::normalize(params.initialForward);
    _left = glm::normalize(glm::cross(_up, _forward));

    _viewMatrix = glm::lookAt(_target + _radius * -1.f * _forward, _target, _up);
}

void Camera::rotateHorizontally(float delta) {
    // rotate camera frame around the up vector
    glm::quat qYaw = glm::angleAxis(delta, _up);

    _forward = qYaw * _forward;
    _left = glm::cross(_up, _forward);
    _left = glm::normalize(_left);

    _viewMatrix = glm::lookAt(_target + _radius * -1.f * _forward, _target, _up);
}

void Camera::rotateVertically(float delta) {
    // rotate camera frame around the left vector
    glm::quat qPitch = glm::angleAxis(delta, _left);

    _forward = qPitch * _forward;
    _up = glm::cross(_forward, _left);
    _up = glm::normalize(_up);

    _viewMatrix = glm::lookAt(_target + _radius * -1.f * _forward, _target, _up);
}

void Camera::changeZoom(float delta) {
    _radius = glm::clamp(_radius + delta, _minRadius, _maxRadius);

    _viewMatrix = glm::lookAt(_target + _radius * -1.f * _forward, _target, _up);
}

glm::mat4 Camera::getViewMatrix() {
    return _viewMatrix;
}

glm::vec3 Camera::getPosition() {
    return _target + -1.f * _radius * _forward;
}

void Camera::setTarget(const glm::vec3& target) {
    if (_isAnimating) {
        _animationEndTarget = target;
    } 
    else {
        _target = target;
        _viewMatrix = glm::lookAt(_target + _radius * -1.f * _forward, _target, _up);
    }
}

void Camera::setTargetAnimated(const glm::vec3& target) {
    _isAnimating = true;
    _animationStartTarget = _target;
    _animationEndTarget = target;
    _animationDuration = 1.0f;
    _animationElapsed = 0.0f;
    // No radius animation: hold current radius across all phases.
    _animationStartRadius = _radius;
    _animationPulloutRadius = _radius;
    _animationEndRadius = _radius;
}

void Camera::switchTargetAnimated(const glm::vec3& target,
                                  float pulloutRadius,
                                  float arrivalRadius,
                                  float duration) {
    _isAnimating = true;
    _animationStartTarget = _target;
    _animationEndTarget = target;
    _animationDuration = duration;
    _animationElapsed = 0.0f;
    _animationStartRadius = _radius;
    _animationPulloutRadius = pulloutRadius;
    _animationEndRadius = arrivalRadius;
}

void Camera::advanceAnimation(float deltaTime) {
    if (!_isAnimating) return;

    _animationElapsed += deltaTime;
    float t = glm::clamp(_animationElapsed / _animationDuration, 0.0f, 1.0f);

    // Three phases: pull out (0–0.33) → travel (0.33–0.67) → zoom in (0.67–1.0).
    constexpr float p1 = 0.33f;
    constexpr float p2 = 0.67f;

    if (t < p1) {
        float phaseT = easeInOutCubic(t / p1);
        _radius = glm::mix(_animationStartRadius, _animationPulloutRadius, phaseT);
        _target = _animationStartTarget;
    }
    else if (t < p2) {
        float phaseT = easeInOutCubic((t - p1) / (p2 - p1));
        _radius = _animationPulloutRadius;
        _target = glm::mix(_animationStartTarget, _animationEndTarget, phaseT);
    }
    else {
        float phaseT = easeInOutCubic((t - p2) / (1.0f - p2));
        _radius = glm::mix(_animationPulloutRadius, _animationEndRadius, phaseT);
        _target = _animationEndTarget;
    }

    _viewMatrix = glm::lookAt(_target + _radius * -1.f * _forward, _target, _up);

    if (t >= 1.0f) {
        _isAnimating = false;
    }
}

float Camera::easeInOutCubic(float t) {
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}