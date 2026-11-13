"""
conftest.py — shared pytest fixtures for MiniOS backend tests.

Key guarantee: every test runs with an empty mock-container store so that
no test leaks state into another.  The real C engine and sudo are NEVER
called because the mock path is taken whenever IS_LINUX is False
(Windows) OR when the engine binary does not exist.
"""
import pytest
from fastapi.testclient import TestClient

import app.services.engine as engine_module
from app.main import app


@pytest.fixture(autouse=True)
def reset_mock_store():
    """
    Clear _MOCK_CONTAINERS before *and* after every test.
    autouse=True means this runs for every test in the suite automatically.
    """
    engine_module._MOCK_CONTAINERS.clear()
    yield
    engine_module._MOCK_CONTAINERS.clear()


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
