import os, sys
import pydantic
from backend.app.schemas.models import CreateContainerRequest, ContainerInspectResponse

print("Pydantic version:", pydantic.__version__)
req = CreateContainerRequest(name="test-p2", nice_value=-5, cpuset="0-1", restart_policy="always")
print("Req parsed:", req.model_dump())

