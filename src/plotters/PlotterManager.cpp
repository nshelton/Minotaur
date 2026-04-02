#include "plotters/PlotterManager.h"

#include <imgui.h>
#include <fmt/format.h>
#include <glog/logging.h>

#include "Page.h"

// ---------- Connection ----------

void PlotterManager::connect(const std::string &port)
{
	std::string err;
	if (m_serial.connect(port, 115200, &err))
	{
		m_ax = std::make_unique<AxiDrawController>(m_serial, m_axState);
		std::string ierr;
		if (!m_ax->initialize(&ierr))
		{
			LOG(ERROR) << "AxiDraw initialize failed: " << ierr;
			m_ax.reset();
			m_serial.disconnect();
		}
	}
}

void PlotterManager::disconnect()
{
	m_ax.reset();
	m_serial.disconnect();
}

bool PlotterManager::isConnected() const
{
	return m_serial.isConnected();
}

// ---------- Job control ----------

void PlotterManager::ensureSpooler()
{
	if (!m_spooler && m_ax)
	{
		m_spooler = std::make_unique<PlotSpooler>(m_serial, *m_ax);
	}
}

bool PlotterManager::startJob(const PageModel &page)
{
	if (!m_ax) return false;
	ensureSpooler();
	if (m_spooler->startJob(page, m_config, /*liftPen=*/true))
	{
		m_showPlotProgress = true;
		return true;
	}
	return false;
}

bool PlotterManager::startJobSingle(const PageModel &page, int entityId)
{
	if (!m_ax) return false;
	ensureSpooler();
	if (m_spooler->startJobSingle(page, entityId, m_config, /*liftPen=*/true))
	{
		m_showPlotProgress = true;
		return true;
	}
	return false;
}

void PlotterManager::pause()
{
	if (m_spooler) m_spooler->pause();
}

void PlotterManager::resume()
{
	if (m_spooler) m_spooler->resume();
}

void PlotterManager::cancel()
{
	if (m_spooler) m_spooler->cancel();
	m_showPlotProgress = false;
}

// ---------- Pen control ----------

void PlotterManager::penUp()
{
	if (m_ax)
	{
		std::string err;
		m_ax->penUp(-1, &err);
	}
}

void PlotterManager::penDown()
{
	if (m_ax)
	{
		std::string err;
		m_ax->penDown(-1, &err);
	}
}

void PlotterManager::disengageMotors()
{
	if (m_ax)
	{
		std::string err;
		m_ax->disengageMotors(&err);
	}
}

// ---------- Config ----------

PlotterConfig &PlotterManager::config()
{
	return m_config;
}

const PlotterConfig &PlotterManager::config() const
{
	return m_config;
}

void PlotterManager::syncStateFromConfig()
{
	m_axState.penUpPos = m_config.penUpPos;
	m_axState.penDownPos = m_config.penDownPos;
}

void PlotterManager::syncConfigFromState()
{
	m_config.penUpPos = m_axState.penUpPos;
	m_config.penDownPos = m_axState.penDownPos;
}

// ---------- Status ----------

bool PlotterManager::isRunning() const
{
	return m_spooler && m_spooler->isRunning();
}

bool PlotterManager::isPaused() const
{
	return m_spooler && m_spooler->isPaused();
}

bool PlotterManager::showPlotProgress() const
{
	return m_showPlotProgress;
}

bool PlotterManager::canPlot() const
{
	return m_ax != nullptr && m_serial.isConnected() && !isRunning();
}

PlotSpooler::Stats PlotterManager::stats() const
{
	if (m_spooler) return m_spooler->stats();
	return {};
}

static const std::vector<Path> kEmptyPaths;

const std::vector<Path> &PlotterManager::getOrderedPaths() const
{
	if (m_spooler) return m_spooler->getOrderedPaths();
	return kEmptyPaths;
}

void PlotterManager::updatePlotProgress()
{
	if (m_showPlotProgress && m_spooler && !m_spooler->isRunning())
	{
		m_showPlotProgress = false;
	}
}

// ---------- Auto-connect polling ----------

void PlotterManager::pollAutoConnect()
{
	if (m_serial.isConnected()) return;
	if (isRunning()) return; // don't disrupt an active job

	auto now = Clock::now();
	if (now - m_lastAutoConnectAttempt < kAutoConnectInterval) return;
	m_lastAutoConnectAttempt = now;

	std::string err;
	std::string chosen;
	if (m_serial.autoConnect(&chosen, 115200, &err))
	{
		strncpy(m_portBuf, chosen.c_str(), sizeof(m_portBuf) - 1);
		m_portBuf[sizeof(m_portBuf) - 1] = '\0';
		m_ax = std::make_unique<AxiDrawController>(m_serial, m_axState);
		std::string ierr;
		if (!m_ax->initialize(&ierr))
		{
			LOG(ERROR) << "AxiDraw initialize failed during auto-connect: " << ierr;
			m_ax.reset();
			m_serial.disconnect();
		}
		else
		{
			LOG(INFO) << "Auto-connected to plotter on " << chosen;
		}
	}
}

// ---------- GUI ----------

void PlotterManager::renderGui(const PageModel &page)
{
	ImGui::Text("Plotter");
	bool connected = m_serial.isConnected();

	// Persistent status indicator
	if (connected)
	{
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1), "[Connected: %s]", m_portBuf);
	}
	else
	{
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "[Scanning...]");
	}

	if (connected)
	{
		if (ImGui::Button("Disconnect"))
		{
			disconnect();
		}

		ImGui::SameLine();
		if (ImGui::Button("Pen Up"))
		{
			penUp();
		}
		ImGui::SameLine();
		if (ImGui::Button("Pen Down"))
		{
			penDown();
		}

		ImGui::SameLine();
		if (ImGui::Button("Disengage Motors"))
		{
			disengageMotors();
		}

		// Pen position sliders
		ImGui::Separator();
		int up = m_axState.penUpPos;
		int down = m_axState.penDownPos;
		if (ImGui::SliderInt("Pen Up Position", &up, 8000, 20000))
		{
			m_axState.penUpPos = up;
			m_config.penUpPos = up;
			if (m_ax)
			{
				std::string e;
				m_ax->setPenUpValue(up, &e);
			}
		}
		if (ImGui::SliderInt("Pen Down Position", &down, 8000, 20000))
		{
			m_axState.penDownPos = down;
			m_config.penDownPos = down;
			if (m_ax)
			{
				std::string e;
				m_ax->setPenDownValue(down, &e);
			}
		}

		// Speed sliders
		ImGui::Separator();
		{
			float drawSpeed = m_config.drawSpeedMmPerS;
			float travelSpeed = m_config.travelSpeedMmPerS;
			float accelDraw = m_config.accelDrawMmPerS2;
			float accelTravel = m_config.accelTravelMmPerS2;

			if (ImGui::SliderFloat("Draw Speed (mm/s)", &drawSpeed, 5.0f, 200.0f, "%.1f"))
			{ m_config.drawSpeedMmPerS = drawSpeed; if (m_spooler && m_spooler->isRunning()) m_spooler->updateConfig(m_config); }
			if (ImGui::SliderFloat("Travel Speed (mm/s)", &travelSpeed, 5.0f, 300.0f, "%.1f"))
			{ m_config.travelSpeedMmPerS = travelSpeed; if (m_spooler && m_spooler->isRunning()) m_spooler->updateConfig(m_config); }
			if (ImGui::SliderFloat("Draw Accel (mm/s^2)", &accelDraw, 50.0f, 5000.0f, "%.0f"))
			{ m_config.accelDrawMmPerS2 = accelDraw; if (m_spooler && m_spooler->isRunning()) m_spooler->updateConfig(m_config); }
			if (ImGui::SliderFloat("Travel Accel (mm/s^2)", &accelTravel, 50.0f, 8000.0f, "%.0f"))
			{ m_config.accelTravelMmPerS2 = accelTravel; if (m_spooler && m_spooler->isRunning()) m_spooler->updateConfig(m_config); }
		}

		if (ImGui::CollapsingHeader("Advanced Motion"))
		{
			float cornering = m_config.cornering;
			int junctionFloor = m_config.junctionSpeedFloorPercent;
			int sliceMs = m_config.timeSliceMs;
			int maxRate = m_config.maxStepRatePerAxis;
			float minSeg = m_config.minSegmentMm;

			if (ImGui::SliderFloat("Cornering (jd, mm)", &cornering, 0.00f, 2.00f, "%.2f"))
			{ m_config.cornering = cornering; if (m_spooler && m_spooler->isRunning()) m_spooler->updateConfig(m_config); }
			if (ImGui::SliderInt("Junction Speed Floor (%)", &junctionFloor, 0, 100))
			{ m_config.junctionSpeedFloorPercent = junctionFloor; if (m_spooler && m_spooler->isRunning()) m_spooler->updateConfig(m_config); }
			if (ImGui::SliderInt("Time Slice (ms)", &sliceMs, 2, 100))
			{ m_config.timeSliceMs = sliceMs; if (m_spooler && m_spooler->isRunning()) m_spooler->updateConfig(m_config); }
			if (ImGui::SliderInt("Max Step Rate (steps/s)", &maxRate, 500, 10000))
			{ m_config.maxStepRatePerAxis = maxRate; if (m_spooler && m_spooler->isRunning()) m_spooler->updateConfig(m_config); }
			if (ImGui::SliderFloat("Min Segment (mm)", &minSeg, 0.01f, 1.0f, "%.2f"))
			{ m_config.minSegmentMm = minSeg; if (m_spooler && m_spooler->isRunning()) m_spooler->updateConfig(m_config); }
		}

		ImGui::Separator();
		ImGui::Text("Job");
		bool spoolerRunning = isRunning();
		if (!spoolerRunning)
		{
			if (ImGui::Button("Start Plot"))
			{
				startJob(page);
			}
		}
		else
		{
			PlotSpooler::Stats s = m_spooler->stats();
			ImGui::Text("Queued: %d  Sent: %d  Plotted(mm): %.1f / %.1f", s.commandsQueued, s.commandsSent, s.donePenDownMm, s.plannedPenDownMm);
			float frac = s.percentComplete;
			if (s.plannedPenDownMm <= 0.0f) {
				frac = 0.0f;
			}
			if (frac < 0.0f) frac = 0.0f; if (frac > 1.0f) frac = 1.0f;
			ImGui::ProgressBar(frac, ImVec2(-1, 0), nullptr);
			// Elapsed and ETA display
			int elapsedSec = s.elapsedMs / 1000;
			int etaSec = s.etaMs / 1000;
			int eMin = elapsedSec / 60, eSec = elapsedSec % 60;
			int rMin = etaSec / 60, rSec = etaSec % 60;
			ImGui::Text("Elapsed: %02d:%02d   ETA: %02d:%02d   %.0f%%", eMin, eSec, rMin, rSec, frac * 100.0f);
			if (!m_spooler->isPaused())
			{
				ImGui::SameLine();
				if (ImGui::Button("Pause"))
				{
					pause();
				}
			}
			else
			{
				ImGui::SameLine();
				if (ImGui::Button("Resume"))
				{
					resume();
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				cancel();
			}
		}
	}
}
