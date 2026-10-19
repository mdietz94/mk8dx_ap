"""MK8DX Archipelago client package.

Lives inside the apworld so it ships in the .apworld zip and is auto-
discovered by Archipelago's Launcher (see ../__init__.py). Importing this
package must never pull Kivy: the apworld is imported on headless
generation hosts too.
"""

__version__ = "0.1.1"
