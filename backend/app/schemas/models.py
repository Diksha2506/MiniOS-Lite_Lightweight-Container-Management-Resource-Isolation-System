from pydantic import BaseModel, field_validator, Field
import re


class CreateContainerRequest(BaseModel):
    name: str = Field(..., min_length=1, max_length=63)
    command: str = Field(default="/bin/sh", max_length=255)
    memory_mb: int = Field(default=0, ge=0, le=32768)
    cpu_pct: int = Field(default=0, ge=0, le=100)
    rootfs: str | None = Field(default=None, max_length=4096)

    @field_validator("name")
    @classmethod
    def name_must_be_safe(cls, v: str) -> str:
        if not re.match(r"^[a-zA-Z0-9_-]+$", v):
            raise ValueError("Name may only contain letters, digits, hyphens and underscores.")
        return v


class ContainerResponse(BaseModel):
    name: str
    pid: int
    state: str
    mem_limit: int
    cpu_limit: int
    rootfs: str | None = None
    network_mode: str | None = None
    container_ip: str | None = None
    interface: str | None = None
    gateway: str | None = None
    namespaces: dict[str, int] | None = None


class StatsResponse(BaseModel):
    name: str
    memory_mb: int | None
    cpu_time_ms: int | None


class ApiResponse(BaseModel):
    ok: bool
    message: str


class NamespaceItem(BaseModel):
    type: str
    available: bool
    inode: int
    path: str


class NamespaceInspectResponse(BaseModel):
    container: str
    pid: int
    namespaces: list[NamespaceItem]
    mock: bool = False


class NetworkInspectResponse(BaseModel):
    container: str
    connected: bool
    ip_address: str
    gateway: str
    bridge: str
    host_interface: str
    container_interface: str
    pid: int
    mock: bool = False
