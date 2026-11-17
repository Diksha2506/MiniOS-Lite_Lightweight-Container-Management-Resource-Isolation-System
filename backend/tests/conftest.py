"""
conftest.py — shared pytest fixtures for MiniOS backend tests.

Key guarantee: every test runs with an empty mock-container store so that
no test leaks state into another.  The real C engine and sudo are NEVER
called — IS_LINUX is patched to False for every test so _run() always
delegates to _mock() regardless of host OS (Linux/WSL2 or Windows).
"""
import pytest
from fastapi.testclient import TestClient

import app.services.engine as engine_module
from app.main import app


@pytest.fixture(autouse=True)
def reset_mock_store():
    """
    Force mock mode and clear _MOCK_CONTAINERS before *and* after every test.

    - Patches IS_LINUX to False so _run() always takes the mock path,
      even when running inside WSL2 / native Linux.
    - Clears _MOCK_CONTAINERS so no state leaks between tests.
    autouse=True means this runs for every test in the suite automatically.
    """
    _orig_is_linux = engine_module.IS_LINUX
    engine_module.IS_LINUX = False          # force mock path on any OS
    engine_module._MOCK_CONTAINERS.clear()
    yield
    engine_module._MOCK_CONTAINERS.clear()
    engine_module.IS_LINUX = _orig_is_linux  # restore after test


@pytest.fixture()
def client():
    """Return a fresh TestClient for each test."""
    return TestClient(app)


@pytest.fixture()
def created_container(client):
    """
    Convenience fixture: creates a container called 'test-box' and returns its name.
    Relies on reset_mock_store so state is always clean beforehand.
    """
    resp = client.post("/api/containers", json={
        "name": "test-box",
        "command": "echo hello",
        "memory_mb": 128,
        "cpu_pct": 50,
    })
    assert resp.status_code == 200, resp.text
    return "test-box"
