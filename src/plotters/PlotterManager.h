#pragma once

#include <memory>
#include <string>
#include <vector>

#include "serial/SerialController.h"
#include "plotters/AxidrawController.h"
#include "plotters/PlotSpooler.h"
#include "plotters/PlotterConfig.h"

struct PageModel;

class PlotterManager
{
public:
	// Connection
	void connect(const std::string &port);
	void disconnect();
	bool isConnected() const;

	// Job control
	bool startJob(const PageModel &page);
	bool startJobSingle(const PageModel &page, int entityId);
	void pause();
	void resume();
	void cancel();

	// Pen control
	void penUp();
	void penDown();
	void disengageMotors();

	// Config access (for serialization)
	PlotterConfig &config();
	const PlotterConfig &config() const;

	// Sync pen positions between AxiDrawState and PlotterConfig
	void syncStateFromConfig();
	void syncConfigFromState();

	// Status queries
	bool isRunning() const;
	bool isPaused() const;
	bool showPlotProgress() const;
	bool canPlot() const;
	PlotSpooler::Stats stats() const;
	const std::vector<Path> &getOrderedPaths() const;

	// Called from MainScreen::onUpdate to clear progress when job ends
	void updatePlotProgress();

	// ImGui panel for plotter controls
	void renderGui(const PageModel &page);

private:
	void ensureSpooler();

	SerialController m_serial{};
	AxiDrawState m_axState{};
	std::unique_ptr<AxiDrawController> m_ax{};
	std::unique_ptr<PlotSpooler> m_spooler{};
	PlotterConfig m_config{};
	char m_portBuf[64] = "";
	bool m_showPlotProgress{false};
};
