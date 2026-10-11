#!/usr/bin/env python3
"""
FM-106 Web MIDI Librarian & Synthesizer Editor Server
Launches a local HTTP server and opens the application in your browser.
"""
import http.server
import socketserver
import webbrowser
import os
import sys

PORT = 8106
DIRECTORY = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "web")

class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DIRECTORY, **kwargs)

    def log_message(self, format, *args):
        # Keep console output tidy
        pass

def main():
    os.chdir(DIRECTORY)
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("", PORT), Handler) as httpd:
        url = f"http://localhost:{PORT}/index.html"
        print("=" * 64)
        print("  FM-106 // SYNTHESIZER LIBRARIAN & EDITOR")
        print("=" * 64)
        print(f"  Web application running at: {url}")
        print("  Connecting directly via Web MIDI to FM-106.")
        print("  Press Ctrl+C to stop the server.")
        print("=" * 64)
        webbrowser.open(url)
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nServer stopped.")

if __name__ == "__main__":
    main()
