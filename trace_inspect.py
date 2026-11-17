from fastapi.testclient import TestClient
from backend.app.main import app

client = TestClient(app)
client.post("/api/containers", json={
    "name": "c-test-trace",
    "command": "/bin/sh",
    "memory_mb": 128,
    "memory_high_mb": 64
})
res = client.get("/api/containers/c-test-trace/inspect")
print("inspect status:", res.status_code)
print("inspect json:", res.json())

