import sys
from fastapi.testclient import TestClient
from app.main import app

c = TestClient(app)
r = c.get("/api/containers/test-phase2-create/inspect")
print("INSPECT_RESULT:", r.json())

