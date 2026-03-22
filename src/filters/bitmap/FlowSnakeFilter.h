#pragma once

#include "../Filter.h"

// A single "snake" agent traverses the image, attracted to dark pixels (ink).
// It eats ink as it moves, leaving a single continuous path that reproduces
// the image tonality through varying line density.
//
// Parameters:
//   step_size    - simulation step distance in mm
//   momentum     - directional inertia (0 = responsive, 1 = straight lines)
//   max_turn     - hard clamp on angular change per step (degrees)
//   look_ahead   - how far the snake senses ink ahead (mm)
//   fov          - angular width of the sensing fan (degrees)
//   eat_radius   - radius of the ink consumption kernel (mm)
//   eat_strength - fraction of ink consumed per visit
//   max_steps    - maximum simulation steps (controls path length)
struct FlowSnakeFilter : public FilterTyped<Bitmap, PathSet>
{
	FlowSnakeFilter()
	{
		m_parameters["step_size"] = FilterParameter{
			"Step Size (mm)", 0.1f, 5.0f, 0.5f};
		m_parameters["momentum"] = FilterParameter{
			"Momentum", 0.0f, 0.99f, 0.7f};
		m_parameters["max_turn"] = FilterParameter{
			"Max Turn (deg)", 5.0f, 180.0f, 45.0f};
		m_parameters["look_ahead"] = FilterParameter{
			"Look Ahead (mm)", 1.0f, 50.0f, 10.0f};
		m_parameters["fov"] = FilterParameter{
			"Field of View (deg)", 10.0f, 360.0f, 180.0f};
		m_parameters["eat_radius"] = FilterParameter{
			"Eat Radius (mm)", 0.1f, 5.0f, 1.0f};
		m_parameters["eat_strength"] = FilterParameter{
			"Eat Strength", 0.01f, 1.0f, 0.3f};
		m_parameters["max_steps"] = FilterParameter{
			"Max Steps", 1000.0f, 500000.0f, 100000.0f};
	}

	const char *name() const override { return "Flow Snake"; }
	uint64_t paramVersion() const override { return m_version.load(); }

	void applyTyped(const Bitmap &in, PathSet &out) const override;
};
