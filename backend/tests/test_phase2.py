import pytest
from app.schemas.models import CreateContainerRequest


def test_phase2_model_validation():
    req = CreateContainerRequest(
        name="valid-c1",
        nice_value=-10,
        cpuset="0-3,5",
        memory_high_mb=512,
        swap_limit_mb=-1,
        restart_policy="on-failure",
        drop_caps=True,
        seccomp_enabled=True,
        io_rbps_limit=1048576,
        io_wbps_limit=2097152,
    )
    assert req.nice_value == -10
    assert req.cpuset == "0-3,5"
    assert req.swap_limit_mb == -1
    assert req.restart_policy == "on-failure"

    with pytest.raises(Exception):
        CreateContainerRequest(name="c2", nice_value=-21)
    with pytest.raises(Exception):
        CreateContainerRequest(name="c3", nice_value=20)
    with pytest.raises(Exception):
        CreateContainerRequest(name="c4", cpuset="0-3; rm -rf /")
    with pytest.raises(Exception):
        CreateContainerRequest(name="c5", restart_policy="sometimes")


def test_create_container_phase2_fields(client):
    name = "test-phase2-create"
    client.delete(f"/api/containers/{name}")

    res = client.post(
        "/api/containers",
        json={
            "name": name,
            "command": "/bin/sh",
            "memory_mb": 128,
            "cpu_pct": 50,
            "nice_value": 5,
            "cpuset": "0-1",
            "memory_high_mb": 64,
            "swap_limit_mb": 128,
            "restart_policy": "always",
            "drop_caps": True,
            "seccomp_enabled": True,
            "io_rbps_limit": 500000,
            "io_wbps_limit": 500000,
        },
    )
    assert res.status_code == 200
    assert res.json()["ok"] is True

    # Inspect endpoint
    inspect_res = client.get(f"/api/containers/{name}/inspect")
    assert inspect_res.status_code == 200
    data = inspect_res.json()
    assert data.get("name") == name
    assert data.get("nice_value") == 5
    assert data.get("cpuset") == "0-1"
    assert data.get("mem_high_mb") == 64
    assert data.get("swap_limit_mb") == 128
    assert data.get("restart_policy") == "always"
    assert data.get("drop_caps") is True
    assert data.get("seccomp_enabled") is True

    # Extended stats endpoint
    stats_res = client.get(f"/api/containers/{name}/extended-stats")
    assert stats_res.status_code == 200
    stats_data = stats_res.json()
    assert stats_data["name"] == name
    assert "cpu_time_ms" in stats_data
    assert "memory_current_mb" in stats_data

    # Cleanup
    client.delete(f"/api/containers/{name}")


def test_inspect_not_found(client):
    res = client.get("/api/containers/nonexistent-container-xyz/inspect")
    assert res.status_code == 404


def test_extended_stats_not_found(client):
    res = client.get("/api/containers/nonexistent-container-xyz/extended-stats")
    assert res.status_code == 404
