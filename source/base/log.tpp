// This file provides a logger with two key functions:
//
// 1. Lines will be ended automatically, so we can do DLOG << "foo";
// and a newline will be inserted, but if we do DLOG << "foo\n"
// then we will still only get one newline (the explicit newline will
// override the automatic one).
//
// 2. We can set up indentation by using INDENTED { ... },
// which causes all output in that block to be indented.


// There are several subtleties in automatically ending lines. The
// DLOG macro creates a DelayedNewline object, which upon destruction
// sends a special token to the stream, which causes the stream to
// print a newline character unless a newline character immediately
// preceeded it (i.e. lines that have already been ended will not be
// ended again since that would leave an empty line).
//
// Streams contain internal buffering so it is important to
// communicate by sending tokens rather than by direct method calls,
// which will be out of sync with the stream if the buffer has not
// been flushed.
//
// We also flush the stream after each newline. This is slightly
// inefficient but very useful for logging purposes.

#pragma once

#include <vector>
#include <string>
#include <sstream>
#include <iostream>

#include "common_types.h"

// The namespace in which the log machinery is defined
#define NS ::indoor_context

// The underying log stream
#define LOG_STREAM NS::LogManager::GetLogStream()

// Log to output
// Note that DelayedFlush must go above DelayedNewline so that the
// newline is flushed aswell.
#define DLOG																								\
	if (NS::LogManager::IsEnabled())													\
		if (NS::LogManager::DelayedFlush __y = 1)								\
			if (NS::LogManager::DelayedNewline __x = LOG_STREAM)	\
				LOG_STREAM

// Log to output, don't append a newline
#define DLOG_N																	\
	if (NS::LogManager::IsEnabled())							\
		if (NS::LogManager::DelayedFlush __y = 1)		\
			LOG_STREAM

// Enable logging in current scope
#define ENABLE_DLOG																									\
	NS::LogManager::ScopedEnabler __enabler__ ## __LINE__ ## _(true)

// Disable logging in current scope
#define DISABLE_DLOG																									\
	NS::LogManager::ScopedEnabler __disabler__ ## __LINE__ ## _(false)

// Enable logging in current scope
#define WITH_DLOG																												\
	if (NS::LogManager::ScopedEnabler __enabler__ ## __LINE__ ## _ = true)

// Disable logging in current scope
#define WITHOUT_DLOG																										\
	if (NS::LogManager::ScopedEnabler __disabler__ ## __LINE__ ## _ = false)

// Increase indent in current scope
#define SCOPED_INDENT																							\
	NS::LogManager::ScopedIndenter __indenter__ ## __LINE__ ## _(0)

// Create a block where logging is increased
#define INDENTED																										\
	if (NS::LogManager::ScopedIndenter __indenter__ ## __LINE__ = 0)

// Increase indent in current scope, print a newline at the end of the
// current scope
#define SPACED_INDENT																							\
	NS::LogManager::ScopedIndenter __indenter__ ## __LINE__ ## _(1)

// Write a string then increase the title
#define TITLE(title) DLOG << title; SCOPED_INDENT;

// Write a string then create a scope in which indentation is increased
#define TITLED(title) DLOG << title; INDENTED

// These tell DREPORT how to print without newlines being appended
#define cout_N cout
#define cerr_N cerr

namespace indoor_context {
	// Represents a generic boost::iostreams character sink with a virtual
	// write() method.
	class GenericCharSink {
	public:
		virtual std::streamsize write(const char* s, std::streamsize n) = 0;
	};

	// Represents global log state
	class LogManager {
	private:
		static bool enabled;
		static int indent_level;  // Global indent level
	public:
		// Get the singleton log stream.
		static std::ostream& GetLogStream();
		// Set the sink for the singleton log stream. LogManager will take
		// ownership of the object's memory.
		static void SetLogSink(GenericCharSink* sink);

		// Flush the log stream
		static bool Flush();

		// Control the indent level
		static void SetIndent(int new_level);
		// Increase current indent level
		static void IncreaseIndent(int n=2) {
			SetIndent(indent_level+n);
		}
		// Decrease current indent level
		static void DecreaseIndent(int n=2) {
			SetIndent(indent_level-n);
		}

		// Returns true if logging enabled
		static bool IsEnabled() { return enabled; }
		// Enable logging. Log messages will be pritned to STDERR
		static void Enable() { enabled = true; }
		// Disable logging. Log messages will be ignored.
		static void Disable() { enabled = false; }

		// Send a special end-line-now token, which produces a newline only
		// if the last token recieved was not a newline.
		static void EndCurrentLine();

		// A ScopedIndenter increases the global indent level in its
		// constructor and decreases it by the same amount in its
		// destructor. To have the log output of a code block indented, use
		// the INDENT macro to declare a stack-allocated ScopedIndenter
		// instance, eg:
		// for (...) {
		//   INDENT;
		//   some_other_stuff();
		//   DLOG << "the value of x is " << x << endl;
		//   ...
		// }
		class ScopedIndenter {
		private:
			int nlines;
		public:
			static const int kIndentIncrement = 2;
			ScopedIndenter(int lines = 0);
			~ScopedIndenter();
			operator bool() { return true; }
		};

		// A ScopedEnabler changes the enabled/disabled state of log output in
		// its constructor and restores the old state in its destructor. It
		// can be used to control which blocks produce debug output.
		class ScopedEnabler {
		private:
			bool oldstate;
		public:
			ScopedEnabler(bool newstate);
			~ScopedEnabler();
			operator bool() { return true; }
		};

		// Sends a special newline command to a specified stream on destruction
		class DelayedNewline {
		public:
			ostream& s;
			DelayedNewline(ostream& o) : s(o) { }
			// Send an "end this line now" token followed by a flush token
			~DelayedNewline();
			// So that we can be used inside an IF block for the DLOG macro
			operator bool() const { return true; }
		};

		// Flushes the log on destruction
		class DelayedFlush {
		public:
			DelayedFlush(int trash) { }
			// Send an "end this line now" token followed by a flush token
			~DelayedFlush() { LogManager::Flush(); }
			// So that we can be used inside an IF block for the DLOG macro
			operator bool() const { return true; }
		};
	};
}  // namespace indoor_context
