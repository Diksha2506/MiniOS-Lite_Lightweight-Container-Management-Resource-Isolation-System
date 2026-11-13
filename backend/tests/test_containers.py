"""
test_containers.py — comprehensive mock-mode tests for the MiniOS container API.

Design rules
------------
* All tests use the `client` and `created_container` fixtures from conftest.py.
* The `reset_mock_store` autouse fixture guarantees each test starts with an
  empty store, so tests are fully isolated.
* The real C engine and `sudo` are NEVER called: on Windows IS_LINUX=False,
  so _run() always delegates to _mock().  Tests also assert mock_mode=True
  via the /api/system endpoint.
* No filesystem mounts, cgroup writes, or privileged operations.
"""
import pytest


# ---------------------------------------------------------------------------
# Mock-mode safety assertion
# ---------------------------------------------------------------------------

class TestMockModeIsolation:
    """Verify that every test run is in mock mode — never the real C engine."""

    def test_system_reports_mock_mode(self, client):
        resp = client.get("/api/system")
        assert resp.status_code == 200
        data = resp.json()
        assert data["mock_mode"] is True, (
            "Tests must run in mock mode. "
            "Do not run the real engine or sudo during tests."
        )

    def test_system_info_fields_present(self, client):
        resp = client.get("/api/system")
        assert resp.status_code == 200
        data = resp.json()
        for field in ("os", "kernel", "arch", "engine_path",
                      "engine_exists", "is_linux", "mock_mode",
                      "cgroups_v2", "namespaces",
                      "mem_total_bytes", "mem_available_bytes"):
            assert field in data, f"Missing field: {field}"

    def test_system_info_advertises_namespaces(self, client):
        """Mock mode advertises the full set of Linux namespace types."""
        resp = client.get("/api/system")
        data = resp.json()
        ns = data["namespaces"]
        assert isinstance(ns, list)
        assert len(ns) > 0


# ---------------------------------------------------------------------------
# Health check
# ---------------------------------------------------------------------------

class TestHealth:
    def test_health_ok(self, client):
        resp = client.get("/api/health")
        assert resp.status_code == 200
        assert resp.json() == {"status": "ok", "service": "container-engine-api"}


# ---------------------------------------------------------------------------
# Container creation — valid inputs
# ---------------------------------------------------------------------------

class TestCreateContainer:
    def test_create_minimal(self, client):
        """Only name is required; defaults apply for command/memory/cpu."""
        resp = client.post("/api/containers", json={"name": "box1"})
        assert resp.status_code == 200
        body = resp.json()
        assert body["ok"] is True
        assert "box1" in body["message"]

    def test_create_with_all_fields(self, client):
        resp = client.post("/api/containers", json={
            "name": "full-box",
            "command": "echo hello",
            "memory_mb": 256,
            "cpu_pct": 75,
        })
        assert resp.status_code == 200
        assert resp.json()["ok"] is True

    def test_created_container_appears_in_list(self, client):
        client.post("/api/containers", json={"name": "listed-box"})
        resp = client.get("/api/containers")
        names = [c["name"] for c in resp.json()]
        assert "listed-box" in names

    def test_create_duplicate_name_returns_400(self, client):
        client.post("/api/containers", json={"name": "dup-box"})
        resp = client.post("/api/containers", json={"name": "dup-box"})
        assert resp.status_code == 400
        assert "already exists" in resp.json()["detail"]

    def test_create_multiple_independent_containers(self, client):
        for i in range(3):
            r = client.post("/api/containers", json={"name": f"multi-{i}"})
            assert r.status_code == 200
        resp = client.get("/api/containers")
        names = [c["name"] for c in resp.json()]
        for i in range(3):
            assert f"multi-{i}" in names


# ---------------------------------------------------------------------------
# Container creation — invalid / boundary inputs (Pydantic validation)
# ---------------------------------------------------------------------------

class TestCreateContainerValidation:
    @pytest.mark.parametrize("bad_name", [
        "../traversal",
        "bad/name",
        "name with space",
        "name!@#",
        "",
    ])
    def test_invalid_name_rejected_422(self, client, bad_name):
        resp = client.post("/api/containers", json={
            "name": bad_name,
            "command": "echo",
            "memory_mb": 64,
            "cpu_pct": 10,
        })
        assert resp.status_code == 422, (
            f"Expected 422 for name={bad_name!r}, got {resp.status_code}"
        )

    @pytest.mark.parametrize("good_name", [
        "simple",
        "with-dash",
        "with_underscore",
        "Mixed123",
        "a" * 63,
    ])
    def test_valid_name_accepted(self, client, good_name):
        resp = client.post("/api/containers", json={"name": good_name})
        assert resp.status_code == 200, (
            f"Expected 200 for name={good_name!r}, got {resp.status_code}: {resp.text}"
        )

    def test_memory_mb_upper_boundary(self, client):
        """32768 MB is the schema maximum."""
        resp = client.post("/api/containers", json={
            "name": "mem-max", "memory_mb": 32768,
        })
        assert resp.status_code == 200

    def test_memory_mb_over_limit_rejected(self, client):
        resp = client.post("/api/containers", json={
            "name": "mem-over", "memory_mb": 32769,
        })
        assert resp.status_code == 422

    def test_cpu_pct_upper_boundary(self, client):
        resp = client.post("/api/containers", json={
            "name": "cpu-max", "cpu_pct": 100,
        })
        assert resp.status_code == 200

    def test_cpu_pct_over_100_rejected(self, client):
        resp = client.post("/api/containers", json={
            "name": "cpu-over", "cpu_pct": 101,
        })
        assert resp.status_code == 422

    def test_negative_memory_rejected(self, client):
        resp = client.post("/api/containers", json={
            "name": "neg-mem", "memory_mb": -1,
        })
        assert resp.status_code == 422

    def test_negative_cpu_rejected(self, client):
        resp = client.post("/api/containers", json={
            "name": "neg-cpu", "cpu_pct": -1,
        })
        assert resp.status_code == 422


# ---------------------------------------------------------------------------
# Listing containers
# ---------------------------------------------------------------------------

class TestListContainers:
    def test_list_empty_on_fresh_store(self, client):
        resp = client.get("/api/containers")
        assert resp.status_code == 200
        assert resp.json() == []

    def test_list_returns_correct_schema(self, client, created_container):
        resp = client.get("/api/containers")
        assert resp.status_code == 200
        containers = resp.json()
        assert len(containers) == 1
        c = containers[0]
        for field in ("name", "pid", "state", "mem_limit", "cpu_limit"):
            assert field in c, f"Missing field: {field}"

    def test_list_new_container_state_is_stopped(self, client, created_container):
        containers = client.get("/api/containers").json()
        c = next(x for x in containers if x["name"] == created_container)
        assert c["state"] == "STOPPED"
        assert c["pid"] == -1


# ---------------------------------------------------------------------------
# Get single container
# ---------------------------------------------------------------------------

class TestGetContainer:
    def test_get_existing_container(self, client, created_container):
        resp = client.get(f"/api/containers/{created_container}")
        assert resp.status_code == 200
        assert resp.json()["name"] == created_container

    def test_get_nonexistent_returns_404(self, client):
        resp = client.get("/api/containers/does-not-exist")
        assert resp.status_code == 404


# ---------------------------------------------------------------------------
# Start container
# ---------------------------------------------------------------------------

class TestStartContainer:
    def test_start_stopped_container(self, client, created_container):
        resp = client.post(f"/api/containers/{created_container}/start")
        assert resp.status_code == 200
        assert resp.json()["ok"] is True

    def test_start_transitions_state_to_running(self, client, created_container):
        client.post(f"/api/containers/{created_container}/start")
        c = client.get(f"/api/containers/{created_container}").json()
        assert c["state"] == "RUNNING"
        assert c["pid"] > 0

    def test_start_already_running_returns_400(self, client, created_container):
        client.post(f"/api/containers/{created_container}/start")
        resp = client.post(f"/api/containers/{created_container}/start")
        assert resp.status_code == 400
        assert "already running" in resp.json()["detail"]

    def test_start_nonexistent_container_returns_400(self, client):
        resp = client.post("/api/containers/ghost/start")
        assert resp.status_code == 400


# ---------------------------------------------------------------------------
# Stop container
# ---------------------------------------------------------------------------

class TestStopContainer:
    def test_stop_running_container(self, client, created_container):
        client.post(f"/api/containers/{created_container}/start")
        resp = client.post(f"/api/containers/{created_container}/stop")
        assert resp.status_code == 200
        assert resp.json()["ok"] is True

    def test_stop_transitions_state_to_stopped(self, client, created_container):
        client.post(f"/api/containers/{created_container}/start")
        client.post(f"/api/containers/{created_container}/stop")
        c = client.get(f"/api/containers/{created_container}").json()
        assert c["state"] == "STOPPED"
        assert c["pid"] == -1

    def test_stop_already_stopped_returns_400(self, client, created_container):
        resp = client.post(f"/api/containers/{created_container}/stop")
        assert resp.status_code == 400
        assert "already stopped" in resp.json()["detail"]

    def test_stop_nonexistent_container_returns_400(self, client):
        resp = client.post("/api/containers/ghost/stop")
        assert resp.status_code == 400


# ---------------------------------------------------------------------------
# Delete container
# ---------------------------------------------------------------------------

class TestDeleteContainer:
    def test_delete_stopped_container(self, client, created_container):
        resp = client.delete(f"/api/containers/{created_container}")
        assert resp.status_code == 200
        assert resp.json()["ok"] is True

    def test_deleted_container_no_longer_listed(self, client, created_container):
        client.delete(f"/api/containers/{created_container}")
        resp = client.get("/api/containers")
        names = [c["name"] for c in resp.json()]
        assert created_container not in names

    def test_delete_running_container_returns_400(self, client, created_container):
        """The engine guards against deleting a running container."""
        client.post(f"/api/containers/{created_container}/start")
        resp = client.delete(f"/api/containers/{created_container}")
        assert resp.status_code == 400
        detail = resp.json()["detail"].lower()
        assert "stop" in detail

    def test_delete_nonexistent_container_returns_400(self, client):
        resp = client.delete("/api/containers/ghost")
        assert resp.status_code == 400

    def test_delete_then_recreate_same_name(self, client, created_container):
        """After deletion, the name must be reusable — no stale state."""
        client.delete(f"/api/containers/{created_container}")
        resp = client.post("/api/containers", json={
            "name": created_container,
            "command": "echo reborn",
        })
        assert resp.status_code == 200


# ---------------------------------------------------------------------------
# Full lifecycle: create -> start -> stop -> delete
# ---------------------------------------------------------------------------

class TestContainerLifecycle:
    def test_full_lifecycle(self, client):
        name = "lifecycle-box"

        r = client.post("/api/containers", json={
            "name": name, "command": "echo hi", "memory_mb": 64, "cpu_pct": 25,
        })
        assert r.status_code == 200

        c = client.get(f"/api/containers/{name}").json()
        assert c["state"] == "STOPPED"

        r = client.post(f"/api/containers/{name}/start")
        assert r.status_code == 200
        c = client.get(f"/api/containers/{name}").json()
        assert c["state"] == "RUNNING"
        assert c["pid"] > 0

        r = client.post(f"/api/containers/{name}/stop")
        assert r.status_code == 200
        c = client.get(f"/api/containers/{name}").json()
        assert c["state"] == "STOPPED"

        r = client.delete(f"/api/containers/{name}")
        assert r.status_code == 200

        assert client.get(f"/api/containers/{name}").status_code == 404
        names = [x["name"] for x in client.get("/api/containers").json()]
        assert name not in names

    def test_restart_after_stop(self, client, created_container):
        """A stopped container must be startable again."""
        client.post(f"/api/containers/{created_container}/start")
        client.post(f"/api/containers/{created_container}/stop")
        r = client.post(f"/api/containers/{created_container}/start")
        assert r.status_code == 200
        c = client.get(f"/api/containers/{created_container}").json()
        assert c["state"] == "RUNNING"


# ---------------------------------------------------------------------------
# Stats endpoint
# ---------------------------------------------------------------------------

class TestStats:
    def test_stats_stopped_container_zero_usage(self, client, created_container):
        resp = client.get(f"/api/containers/{created_container}/stats")
        assert resp.status_code == 200
        data = resp.json()
        assert data["name"] == created_container
        assert data["memory_mb"] == 0
        assert data["cpu_time_ms"] == 0

    def test_stats_running_container_nonnull(self, client, created_container):
        client.post(f"/api/containers/{created_container}/start")
        resp = client.get(f"/api/containers/{created_container}/stats")
        assert resp.status_code == 200
        data = resp.json()
        assert data["memory_mb"] is not None
        assert data["cpu_time_ms"] is not None
        assert data["memory_mb"] >= 0
        assert data["cpu_time_ms"] >= 0

    def test_stats_nonexistent_container_null_values(self, client):
        """Stats for a missing container returns null metrics (engine pattern)."""
        resp = client.get("/api/containers/ghost/stats")
        assert resp.status_code == 200
        data = resp.json()
        assert data["memory_mb"] is None
        assert data["cpu_time_ms"] is None


# ---------------------------------------------------------------------------
# WebSocket log streaming
# ---------------------------------------------------------------------------

class TestWebSocketLogs:
    def test_path_traversal_rejected(self, client):
        with client.websocket_connect("/api/containers/bad-../logs") as ws:
            msg = ws.receive_text()
            assert "Error" in msg or "Invalid" in msg

    def test_valid_name_delivers_mock_logs(self, client, created_container):
        """On non-posix OS, mock log lines are streamed immediately."""
        with client.websocket_connect(
            f"/api/containers/{created_container}/logs"
        ) as ws:
            msg = ws.receive_text()
            assert len(msg) > 0
            assert created_container in msg or "Mock" in msg


# ---------------------------------------------------------------------------
# Store isolation between tests
# ---------------------------------------------------------------------------

class TestStoreIsolation:
    def test_first_test_creates_container(self, client):
        r = client.post("/api/containers", json={"name": "iso-box"})
        assert r.status_code == 200

    def test_second_test_sees_empty_store(self, client):
        """Store must be empty — previous test containers must not leak."""
        resp = client.get("/api/containers")
        assert resp.json() == [], (
            "State leaked from a previous test! "
            "Check that reset_mock_store autouse fixture is working."
        )
