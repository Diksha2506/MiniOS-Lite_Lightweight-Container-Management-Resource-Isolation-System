from backend.app.main import app
from fastapi.testclient import TestClient

client = TestClient(app)
r = client.get("/api/containers/dbg1/inspect")
print("status:", r.status_code)
print("json:", r.json())

