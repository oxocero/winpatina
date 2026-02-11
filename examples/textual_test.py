"""Simple full-screen Textual TUI for testing WinPatina terminal size detection."""
import sys

# Force UTF-8 output through pipes
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

from textual.app import App, ComposeResult
from textual.widgets import Header, Footer, Static


class SizeDisplay(Static):
    """Widget that shows current terminal dimensions."""

    def on_mount(self) -> None:
        self.update_size()

    def on_resize(self) -> None:
        self.update_size()

    def update_size(self) -> None:
        app_w = self.app.size.width
        app_h = self.app.size.height
        self.update(
            f"Textual app size: {app_w} x {app_h}\n\n"
            "If the app fills the whole console window, it works!\n"
            "Try resizing the window to see if it updates.\n\n"
            "Press Q to quit."
        )


class TestApp(App):
    """A minimal full-screen Textual app."""

    TITLE = "WinPatina TUI Test"
    BINDINGS = [("q", "quit", "Quit")]

    CSS = """
    SizeDisplay {
        content-align: center middle;
        text-align: center;
        width: 100%;
        height: 100%;
    }
    """

    def compose(self) -> ComposeResult:
        yield Header()
        yield SizeDisplay()
        yield Footer()


if __name__ == "__main__":
    TestApp().run()
