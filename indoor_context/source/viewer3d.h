#pragma once

#include <map>

#include <boost/function.hpp>

#include "common_types.h"
#include "glut_window.h"

namespace indoor_context {
	// Forward declarations
	class TextureManager;
	class GluProjector;
	class Widget3D;


	// Represents a viewer that manages the rendering of and interaction
	// with a bunch of widgets.
	class Viewer3D {
	public:
		// TODO: make all these private
		// Center around which the camera rotates
		toon::Vector<3> viewCentre;
		// Rotation of the camera around viewCentre
		toon::Vector<3> viewRotation;
		// Dist of camera from center (stored as logarithm)
		double viewLogDist;
		// Whether the projection is orthographic or perspective (default)
		bool viewOrtho;
		// Whether the viewer is configured (window created etc)
		bool configured;
		// Whether the current invokation of OnRender() is the antialiasing phase
		bool isAntialiasing;

		// Now delegates to GlutWindow::Init()
		static void Init(int *argc, char **argv);

		// Constructor and destructor
		Viewer3D();
		Viewer3D(const string& title);
		~Viewer3D();

		// Get our parent window
		GlutWindow& window() { return *window_; }
		const GlutWindow& window() const { return *window_; }

		// Get our texture manager
		inline TextureManager& textures() { return *textures_; }
		
		// Setup the window events etc. This will be run automatically by
		// Run() but you may want to invoke it explicitly so that you can
		// make some GL calls before you call Run().
		void Create();
		// Force the screen to be redrawn
		void Invalidate();
		// Run the GLUT loop. Never returns.
		void Run();
		// Show the viewer in a GLUT window, starting the GLUT event loop
		// in a parallel thread if necessary.
		void RunAsync();

		// Convenience function to create a window and add a single widget to it
		static void RunWidget(Widget3D& w);

		// Get the mouse location
		inline const toon::Vector<2>& mouse_location() const { return window().mouse_location(); }

		// Control whether the mouse navigates the environment
		void SetNavigable(bool v);
		// If true, the mouse navigates around the environment
		bool navigable() const { return navigable_; }

		// Set the background color
		void SetBgColor(const PixelRGB<byte>& color);
		// Get the current background color
		const PixelRGB<byte>& bgcolor() const { return bgcolor_; }

		// Set the currently selected widget
		void Select(Widget3D& widget);
		// Deselect all widgets
		void Deselect();
		// Get the selected widget, or NULL if there isn't one
		inline Widget3D* selection() const { return selection_; }

		// Add a widget
		Widget3D& Add(Widget3D& child, char toggleKey=0);
		// Add a widget, and delete it when this object is destroyed
		Widget3D& AddOwned(Widget3D* child, char toggleKey=0);
		// Add a function as a widget. This function will be called on
		// each redraw. It gets treated like a widget that can't recieve
		// focus or do any of the other fancy things that a full widget
		// implementation can do. Use boost:bind to have parameters passed
		// to this method:
		//
		// void mydraw(Foo myFoo, const string& bar, int baz) { ... }
		//
		// int main(...) {
		//   Viewer3D myVizualization;
		//   myVizualization.Add(boost::bind(&mydraw, theFoo, "some string", 12345));
		// }
		//
		// Returns a widget that wraps this function
		Widget3D& Add(boost::function<void()> renderFunc, char toggleKey=0);
		// Get the children of this widget
		const vector<Widget3D*>& children() const;


		// Get the widget at a given mouse location, or null if there isn't one there
		Widget3D* GetWidgetAt(const toon::Vector<2>& mouse);

		// Reset the GL projection matrix, usually after window resize
		void ConfigureProjection();

		// Project an object location to the screen. (0,0) is top left corner (i.e. not GL std).
		toon::Vector<2> ProjectToScreen(const toon::Vector<4>& obj) const;
		// Project an object location to the screen, assuming the last coord is 1.0.
		toon::Vector<2> ProjectToScreen(const toon::Vector<3>& obj) const;

		// Convert a mouse location to a viewport location
		toon::Vector<2> WindowToViewport(const toon::Vector<2>& screenPt) const;

		// Project a mouse location in window coordinates to a plane in
		// 3D. Mouse coords should be as passed to glutMouseMotion
		// etc. Returns a 3D point on the plane specified by planeEqn.
		toon::Vector<3> MouseToPlane(const toon::Vector<2> mousePt,
													 const toon::Vector<4>& planeEqn);

		// Handlers for GLUT callbacks
		void Window_Display();
		void Window_MouseMove(toon::Vector<2> mousePt);
		void Window_MouseUp(int button, toon::Vector<2> mousePt);
		void Window_MouseDown(int button, toon::Vector<2> mousePt);
		void Window_MouseDrag(int button, toon::Vector<2> mousePt);
		void Window_Click(int button, toon::Vector<2> mousePt);
		void Window_DoubleClick(int button, toon::Vector<2> mousePt);
		void Window_SizeChanged();
	private:
		// The window we're displayed in
		initialized_ptr<GlutWindow> window_;
		// Our texture manager
		initialized_ptr<TextureManager> textures_;
		// The root widget that contains all other widgets
		initialized_ptr<Widget3D> rootWidget_;
		// The projector that holds GL matrices
		mutable initialized_ptr<GluProjector> projector_;

		// Previous mouse location, used during mouse drag
		toon::Vector<2> prevMousePt_;
		// The selected widget, or NULL if nothing is selected
		Widget3D* selection_;
		// The widget the mouse is hovering over, or NULL if nothing is selected
		Widget3D* hoverWidget_;
		// Background color
		PixelRGB<byte> bgcolor_;
		// Whether the mouse navigates in 3D
		bool navigable_;
		// The current GLUT cursor
		int cursor_;
	};




	// Caches the modelview, projection, and viewport matrices for
	// repeated gluProject() calls. Must be very careful about storing
	// these, as each of the OnRender() functions probably changes the
	// viewport.
	class GluProjector {
	public:
		ImageRef windowSize;
		GLdouble modelview[16];
		GLdouble projection[16];
		GLint viewport[4];
		// Create an empty projector (must call Configure before Project)
		GluProjector();
		// Initialize a projector for the current view
		GluProjector(const ImageRef& winSize);
		// Loads the matrices from GL
		void Configure(const ImageRef& winSize);
		// Projects a point through the current matrices
		toon::Vector<2> Project(const toon::Vector<3>& v) const;
	};





	// Manages loading, unloading, and binding GL textures
	class TextureManager {
	public:
		// Select the texture for a given image. If the image has not yet
		// been loaded into GL then it will be loaded under a generated
		// texture ID.
		void Select(const ImageBundle* image);
		// Load an image as a GL texture or look up its texture ID if it
		// has already been loaded.
		GLuint LoadOrLookup(const ImageBundle* image);
		// Reload a texture into GL (or load it for the first time if has
		// not been loaded).
		GLuint Reload(const ImageBundle* image);
		// Remove a texture from GL (or do nothing if it has not been loaded)
		void Delete(const ImageBundle* image);
		// Draw a texture
		void Render(const ImageBundle* image, const GlutWindow* window);
		// Draw a texture full-screen
		void RenderFullScreen(const ImageBundle* image, const GlutWindow* window);
	private:
		// Map of images to texture IDs
		map<const ImageBundle*,unsigned> textureIds_;
	};
}