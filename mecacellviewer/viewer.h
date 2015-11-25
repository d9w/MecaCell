#ifndef QTVIEWER_H
#define QTVIEWER_H
#include "signalslotbase.h"
#include "keyboardmanager.hpp"
#include "mousemanager.hpp"
#include "paintstep.hpp"
#include "screenmanager.hpp"
#include "camera.hpp"
#include "button.hpp"
#include "viewtools.h"
#include "arrowsgroup.hpp"
#include "cellgroup.hpp"
#include "deformableCellGroup.hpp"
#include "gridviewer.hpp"
#include "plugins.hpp"
#include "skybox.hpp"
#include "msaa.hpp"
#include "ssao.hpp"
#include "blur.hpp"
#include <QMap>
#include <QOpenGLFramebufferObject>
#include <QMatrix4x4>
#include <QQuickView>
#include <QQmlContext>
#include <QGuiApplication>
#include <QApplication>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQuickItem>
#include <QQmlContext>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <utility>
#include <functional>

#define MECACELL_VIEWER
#include "macros.h"

using namespace std;
namespace MecacellViewer {
template <typename Scenario> class Viewer : public SignalSlotRenderer {
	friend class SignalSlotBase;

 public:
	using World =
	    typename remove_reference<decltype(((Scenario *)nullptr)->getWorld())>::type;
	using Cell = typename World::cell_type;
	using Vec = decltype(((Cell *)nullptr)->getPosition());
	using ModelType = typename World::model_type;
	using R = Viewer<Scenario>;
	using Rfunc = std::function<void(R *)>;
	using ButtonType = Button<R>;

	Viewer(int c, char **v) : argc(c), argv(v) {
#if __APPLE__
#include "TargetConditionals.h"
#if TARGET_OS_MAC
		// compatibility profile (Qt's default) is not available on mac os...
		// we have to use a core profile
		QSurfaceFormat f;
		f.setProfile(QSurfaceFormat::CoreProfile);
		f.setVersion(3, 3);
		f.setAlphaBufferSize(8);
		f.setRenderableType(QSurfaceFormat::OpenGL);
		QSurfaceFormat::setDefaultFormat(f);
#endif
#endif
		registerPlugin(km);
		registerPlugin(mm);
	};
	// default "plugins"
	KeyboardManager km;
	MouseManager mm;

	int argc;
	char **argv;
	Scenario scenario;
	int frame = 0;

	// Visual elements & config
	Camera camera;
	float screenScaleCoef = 1.0;
	bool fullscreenMode = false;
	unsigned long leftMenuSize = 200;
	QOpenGLFramebufferObject *currentFBO = nullptr;
	QSize viewportSize;
	QMatrix4x4 viewMatrix, projectionMatrix;

	// Events
	int mouseWheel = 0;
	QPointF mousePosition, mousePrevPosition;
	QFlags<Qt::MouseButtons> mouseClickedButtons, mouseDblClickedButtons,
	    mousePressedButtons;
	std::set<Qt::Key> keyDown, keyPress;

	// Stats
	std::chrono::time_point<std::chrono::high_resolution_clock> t0, tfps;
	double viewDt;
	int nbFramesSinceLastTick = 0;
	unsigned long currentFrameNumber = 0;
	Cell *selectedCell = nullptr;
	bool worldUpdate = true;
	bool loopStep = true;
	double fpsRefreshRate = 0.4;
	QVariantMap guiCtrl, stats;
	QList<QVariant> enabledPaintSteps;
	std::vector<std::pair<QList<QVariant>, bool>> displayMenuToggled;

	MenuElement<R> displayMenu;
	bool displayMenuChanged = true;

 public:
	std::vector<Rfunc> plugins_onLoad;
	std::vector<Rfunc> plugins_preLoop;
	std::vector<Rfunc> plugins_preDraw;
	std::vector<Rfunc> plugins_onDraw;
	std::vector<Rfunc> plugins_postDraw;

 private:
	std::map<Qt::Key, Rfunc> keyDownMethods;
	std::map<Qt::Key, Rfunc> keyPressMethods;
	std::map<Qt::MouseButton, Rfunc> mouseDragMethods;
	std::map<Qt::MouseButton, Rfunc> mouseClickMethods;
	std::map<QString, Button<R>> buttons;

	// this is just so we can store paint steps instances without making a mess
	std::map<QString, unique_ptr<PaintStep<R>>> paintSteps;

	// the actual paint steps method to be called
	std::map<int, Rfunc> paintStepsMethods;

	bool paintStepsNeedsUpdate =
	    true;  // do we need to refresh the list of checkable paint steps?

	// screen managers might affect the display. Usually manipulate fbos
	// Inherit from paintStep because they also usually need to be called
	// during painting. Ex: screen space ambient oclusion defines some fbos,
	// makes operations on them and draw an object (a texture) to the screen.
	std::vector<ScreenManager<R> *> screenManagers;

	// init function for the renderer. Create all the defaults paint steps and
	// screen managers, initializes scenario and users additions.
	using psptr = std::unique_ptr<PaintStep<R>>;
	virtual void initialize(QQuickWindow *wdw) {
		MenuElement<R> cellsMenu = {
		    "Cells",
		    {{"Mesh type",
		      elementType::exclusiveGroup,
		      {{"None", false},
		       {"Centers only", false},
		       {"Sphere", false},
		       {"Deformable mesh", true}}},
		     {"Colors", elementType::exclusiveGroup, {{"Normal", true}, {"Pressure", false}}},
		     {"Display forces", false},
		     {"Display velocities", false}}};

		qDebug() << cellsMenu.toJSON();
		this->window = wdw;
		viewportSize = QSize(static_cast<int>(wdw->width()), static_cast<int>(wdw->height()));
		scenario.init(argc, argv);
		GL = QOpenGLContext::currentContext()->functions();
		GL->initializeOpenGLFunctions();
		////////////////////////////////
		// list of default paint steps
		/////////////////////////////////
		paintSteps.emplace("MSAA", psptr(new MSAA<R>(this)));
		paintSteps.emplace("Skybox", psptr(new Skybox<R>()));
		paintSteps.emplace("Cells", psptr(new DeformableCellGroup<R>()));
		paintSteps.emplace("Arrows", psptr(new ArrowsGroup<R>()));
		paintSteps.emplace(
		    "Grids", psptr(new GridViewer<R>(":shaders/mvp.vert", ":/shaders/flat.frag")));
		paintSteps.emplace("SSAO", psptr(new SSAO<R>(this)));
		paintSteps.emplace("Blur", psptr(new MenuBlur<R>(this)));
		screenManagers.push_back(dynamic_cast<ScreenManager<R> *>(paintSteps["MSAA"].get()));
		screenManagers.push_back(dynamic_cast<ScreenManager<R> *>(paintSteps["SSAO"].get()));
		screenManagers.push_back(dynamic_cast<ScreenManager<R> *>(paintSteps["Blur"].get()));

		// TODO: absolutely TERRIBLE performance wise, cool ease-of-use wise. Enhance!
		cellsMenu.onToggled = [&](R *r, MenuElement<R> *me) {
			if (me->isChecked()) {
				if (me->at("Colors").at("Normal").isChecked()) {
					paintStepsMethods[10] = [&](R *r) {
						DeformableCellGroup<R> *cells =
						    dynamic_cast<DeformableCellGroup<R> *>(paintSteps["Cells"].get());
						cells->call(r, "normal");
					};
				} else {
					paintStepsMethods[10] = [&](R *r) {
						DeformableCellGroup<R> *cells =
						    dynamic_cast<DeformableCellGroup<R> *>(paintSteps["Cells"].get());
						cells->call(r, "pressure");
					};
				}
			} else {
				paintStepsMethods.erase(10);
			}
		};
		cellsMenu.at("Display forces").onToggled = [&](R *r, MenuElement<R> *me) {
			if (me->isChecked()) {
				paintStepsMethods[15] = [&](R *r) {
					ArrowsGroup<R> *arrows =
					    dynamic_cast<ArrowsGroup<R> *>(paintSteps["Arrows"].get());
					auto f0 = r->scenario.getWorld().getAllForces();
					vector<pair<QVector3D, QVector3D>> f;
					f.reserve(f0.size());
					for (auto &p : f0) {
						f.push_back(make_pair(toQV3D(p.first), toQV3D(p.second)));
					}
					arrows->call(r, f, QVector4D(1.0, 0.3, 0.6, 1.0));
				};
			} else {
				paintStepsMethods.erase(15);
			}
		};
		displayMenu = cellsMenu;
		qDebug() << "before onload :";
		displayMenu.print();
		for (auto &p : plugins_onLoad) p(this);
		qDebug() << "after onload :";
		displayMenu.print();
		displayMenu.callAll(this);
	}

	void applyInterfaceAdditions(SignalSlotBase *b) {
		QObject *root = b->parentItem();
		for (auto &b : buttons) {
			auto &bt = b.second;
			if (bt.needsToBeUpdated()) {
				QMetaObject::invokeMethod(root, "addButton", Q_ARG(QVariant, bt.getName()),
				                          Q_ARG(QVariant, bt.getMenu()),
				                          Q_ARG(QVariant, bt.getLabel()),
				                          Q_ARG(QVariant, bt.getColor()));
				bt.updateOK();
			}
		}
		if (displayMenuChanged) {
			QMetaObject::invokeMethod(root, "createDisplayMenu",
			                          Q_ARG(QVariant, displayMenu.toJSON()));
			displayMenuChanged = false;
		}
	}
	// called after every frame, thread safe
	// synchronization between Qt threads
	virtual void sync(SignalSlotBase *b) {
		applyInterfaceAdditions(b);

		// loop
		worldUpdate = b->worldUpdate;
		loopStep = b->loopStep;
		b->loopStep = false;

		guiCtrl = b->getGuiCtrl();

		// stats
		if (selectedCell)
			stats["selectedCell"] = cellToQVMap(selectedCell);
		else
			stats.remove("selectedCell");
		b->setStats(stats);
		b->statsChanged();

		// menu
		displayMenuToggled = b->displayMenuToggled;
		b->displayMenuToggled.clear();
		displayMenu.updateCheckedFromList(this, displayMenuToggled);
		if (displayMenuToggled.size() > 0) displayMenu.callAll(this);

		// mouse
		mouseClickedButtons = b->mouseClickedButtons;
		b->mouseClickedButtons &= Qt::NoButton;
		mouseDblClickedButtons = b->mouseDblClickedButtons;
		b->mouseDblClickedButtons &= Qt::NoButton;
		mousePrevPosition = mousePosition;
		mousePosition = b->lastMouseEvent.localPos();
		if (mouseClickedButtons > 0) {
			mousePrevPosition = mousePosition;
		}
		mousePressedButtons = b->lastMouseEvent.buttons();
		mouseWheel = b->mouseWheel;
		b->mouseWheel = 0;

		// keyboard
		keyPress = b->keyPress;
		keyDown = b->keyDown;
		b->keyPress.clear();
		processEvents(b);
	}

	/***********************************
	 *              EVENTS              *
	 ***********************************/
	// events handling routine
	void processEvents(SignalSlotBase *b) {
		const vector<Qt::MouseButton> acceptedButtons = {
		    {Qt::LeftButton, Qt::RightButton, Qt::MiddleButton}};
		// mouse drag (mouse down)
		for (auto &b : acceptedButtons) {
			if (mousePressedButtons.testFlag(b) && mouseDragMethods.count(b)) {
				mouseDragMethods[b](this);
			}
		}
		// mouse click
		for (auto &b : acceptedButtons) {
			if (mouseClickedButtons.testFlag(b) && mouseClickMethods.count(b)) {
				mouseClickMethods[b](this);
			}
		}
		// keyboard press (only once per key press)
		for (auto &k : keyPress) {
			if (keyPressMethods.count(k)) {
				keyPressMethods.at(k)(this);
			}
		}
		// keyboard down (key is down)
		for (auto &k : keyDown) {
			if (keyDownMethods.count(k)) {
				keyDownMethods.at(k)(this);
			}
		}
		// buttons
		for (auto &bName : b->clickedButtons) {
			if (buttons.count(bName)) {
				buttons[bName].clicked(this);
			}
		}
		b->clickedButtons.clear();
	}

	QVariantMap cellToQVMap(Cell *c) {
		QVariantMap res;
		if (c) {
			res["Radius"] = c->getBoundingBoxRadius();
			res["Volume"] = c->getVolume();
			res["Pressure"] = c->getPressure();
			res["Mass"] = c->getMass();
			res["Connections"] = c->getNbConnections();
		}
		return res;
	}

	/***************************************************
	 * ** ** ** ** *         PAINT       * ** ** ** ** *
	 **************************************************/
	virtual void paint() {
		viewMatrix = camera.getViewMatrix();
		projectionMatrix = camera.getProjectionMatrix((float)viewportSize.width() /
		                                              (float)viewportSize.height());
		updateScenario();
		// default paint Methods
		paintStepsMethods[0] = [&](R *r) { paintSteps["MSAA"]->call(r); };
		paintStepsMethods[5] = [&](R *r) { paintSteps["Skybox"]->call(r); };
		paintStepsMethods[1000000] = [&](R *r) { paintSteps["SSAO"]->call(r); };
		paintStepsMethods[2000000] = [&](R *r) { paintSteps["Blur"]->call(r); };

		for (auto &p : plugins_preDraw) p(this);

		for (auto &s : paintStepsMethods) {
			s.second(this);
		}

		for (auto &p : plugins_postDraw) p(this);

		updateStats();
		if (window) {
			window->resetOpenGLState();
			window->update();
		}
	}

	void updateStats() {
		auto t1 = std::chrono::high_resolution_clock::now();
		auto fpsDt = t1 - tfps;
		nbFramesSinceLastTick++;
		if (fpsDt.count() > fpsRefreshRate) {
			stats["fps"] = (double)nbFramesSinceLastTick / (double)fpsDt.count();
			nbFramesSinceLastTick = 0;
			tfps = chrono::high_resolution_clock::now();
		}
		stats["nbCells"] = QVariant((int)scenario.getWorld().cells.size());
		stats["nbUpdates"] = scenario.getWorld().getNbUpdates();
		if (window) {
			window->resetOpenGLState();
		}
		std::chrono::duration<double> dv = t1 - t0;
		viewDt = dv.count();
		t0 = std::chrono::high_resolution_clock::now();
		camera.updatePosition(viewDt);
		++frame;
	}

	// called on redimensioning events.
	void setViewportSize(const QSize &s) {
		viewportSize = s;
		screenScaleCoef = 1.0;  // window->devicePixelRatio();
		for (auto &sm : screenManagers) {
			sm->screenChanged(this);
		}
	}

	void updateScenario() {
		if (loopStep || worldUpdate) {
			for (auto &p : plugins_preLoop) p(this);
			scenario.loop();
			if (!selectedCellStillExists()) selectedCell = nullptr;
			loopStep = false;
		}
	}

 public:
	/**************************
	 *           SET
	 **************************/
	void setCurrentFBO(QOpenGLFramebufferObject *fbo) { currentFBO = fbo; }
	void setSelectedCell(Cell *c) { selectedCell = c; }
	/**************************
	 *           GET
	 **************************/
	Scenario &getScenario() { return scenario; }
	const QMatrix4x4 &getViewMatrix() { return viewMatrix; }
	const QMatrix4x4 &getProjectionMatrix() { return projectionMatrix; }
	double getTimeSinceLastFrame() { return viewDt; }
	Camera &getCamera() { return camera; }
	Cell *getSelectedCell() { return selectedCell; }
	QSize getViewportSize() { return viewportSize; }
	QOpenGLFramebufferObject *getCurrentFBO() { return currentFBO; }
	float getScreenScaleCoef() { return screenScaleCoef; }
	unsigned long getCurrentFrame() { return currentFrameNumber; }
	bool isFullscreen() { return fullscreenMode; }
	unsigned long getLeftMenuSize() { return leftMenuSize; }
	bool selectedCellStillExists() {
		return (std::find(scenario.getWorld().cells.begin(), scenario.getWorld().cells.end(),
		                  selectedCell) != scenario.getWorld().cells.end());
	}
	MenuElement<R> *getDisplayMenu() { return &displayMenu; }

	/*************************
	 *    UI ADDITIONS
	 *************************/
	template <typename P> void registerPlugin(P &p) { loadPluginHooks(this, p); }
	void addKeyDownMethod(Qt::Key k, Rfunc f) { keyDownMethods[k] = f; }
	void addKeyPressMethod(Qt::Key k, Rfunc f) { keyPressMethods[k] = f; }
	void addMouseDragMethod(Qt::MouseButton b, Rfunc f) { mouseDragMethods[b] = f; }
	void addMouseClickMethod(Qt::MouseButton b, Rfunc f) { mouseClickMethods[b] = f; }
	QPointF getMousePosition() { return mousePosition; }
	QPointF getPreviousMousePosition() { return mousePrevPosition; }
	Button<R> *addButton(Button<R> b) {
		buttons[b.getName()] = b;
		return &buttons[b.getName()];
	}
	Button<R> *addButton(std::string name, std::string menu, std::string label,
	                     std::function<void(R *, Button<R> *)> onClicked) {
		Button<R> b(QString::fromStdString(name), QString::fromStdString(menu),
		            QString::fromStdString(label), onClicked);
		buttons[QString::fromStdString(name)] = b;
		return &buttons[b.getName()];
	}
	Button<R> *getButton(std::string name) {
		if (buttons.count(QString::fromStdString(name)))
			return &buttons[QString::fromStdString(name)];
		return nullptr;
	}
	QQuickWindow *getWindow() { return window; }

	int exec() {
		QGuiApplication app(argc, argv);
		app.setQuitOnLastWindowClosed(true);
		QQuickView view;
		view.setFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowMinMaxButtonsHint |
		              Qt::WindowTitleHint | Qt::WindowCloseButtonHint |
		              Qt::WindowFullscreenButtonHint);
		view.setSurfaceType(QSurface::OpenGLSurface);
		view.setColor(QColor(Qt::transparent));
		view.setClearBeforeRendering(true);
		view.setResizeMode(QQuickView::SizeRootObjectToView);
		qmlRegisterType<SignalSlotBase>("SceneGraphRendering", 1, 0, "Renderer");
		view.setSource(QUrl("qrc:/main.qml"));
		QObject *root = view.rootObject();
		SignalSlotBase *ssb = root->findChild<SignalSlotBase *>("renderer");
		view.rootContext()->setContextProperty("glview", ssb);
		ssb->init(this);
		view.show();
		return app.exec();
	}
};
}
#endif
