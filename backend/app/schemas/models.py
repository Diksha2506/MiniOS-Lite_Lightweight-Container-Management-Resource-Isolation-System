from pydantic import BaseModel, field_validator, Field
import re


class CreateContainerRequest(BaseModel):
    name: str = Field(..., min_length=1, max_length=63)
    command: str = Field(default="/bin/sh", max_length=255)
    memory_mb: int = Field(default=0, ge=0, le=32768)
    cpu_pct: int = Field(default=0, ge=0, le=100)
    rootfs: str | None = Field(default=None, max_length=4096)

    # Phase 2 — resource isolation fields
    nice_value: int = Field(
        default=0,
        ge=-20,
        le=19,
        description="Process niceness (-20 highest priority, 19 lowest)",
    )
    cpuset: str = Field(
        default="",
        max_length=63,
        description="CPU affinity set, e.g. 0-3 or 0,2",
    )
    memory_high_mb: int = Field(
        default=0,
        ge=0,
        le=1048576,
        description="Soft memory high-watermark (cgroup v2 memory.high)",
    )
    swap_limit_mb: int = Field(
        default=0,
        ge=-1,
        le=1048576,
        description="Swap limit in MB; -1 means unlimited",
    )
    restart_policy: str = Field(
        default="never",
        pattern="^(never|on-failure|always)$",
        description="Supervisor restart policy",
    )
    drop_caps: bool = Field(
        default=False,
        description="Drop all Linux capabilities before exec",
    )
    seccomp_enabled: bool = Field(
        default=False,
        description="Apply default seccomp syscall filter",
    )
    io_rbps_limit: int = Field(
        default=0,
        ge=0,
        description="I/O read bytes-per-second limit (0 = unlimited)",
    )
    io_wbps_limit: int = Field(
        default=0,
        ge=0,
        description="I/O write bytes-per-second limit (0 = unlimited)",
    )

    @field_validator("name")
    @classmethod
    def name_must_be_safe(cls, v: str) -> str:
        if not re.match(r"^[a-zA-Z0-9_-]+$", v):
            raise ValueError("Name may only contain letters, digits, hyphens and underscores.")
        return v

    @field_validator("cpuset")
    @classmethod
    def cpuset_format(cls, v: str) -> str:
        if v and not re.match(r"^[\d,-]+$", v):
            raise ValueError("cpuset must contain only digits, commas and hyphens (e.g. '0-3' or '0,2')")
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


class ContainerInspectResponse(BaseModel):
    name: str
    pid: int
    state: str
    mem_limit_mb: int
    mem_high_mb: int
    swap_limit_mb: int
    cpu_limit_pct: int
    cpuset: str
    nice_value: int
    restart_policy: str
    drop_caps: bool
    seccomp_enabled: bool
    io_rbps_limit: int
    io_wbps_limit: int
    rootfs: str | None = None
    mock: bool = False


class PhaseStatsResponse(BaseModel):
    name: str
    memory_current_mb: int | None = None
    memory_peak_mb: int | None = None
    swap_current_mb: int | None = None
    cpu_time_ms: int | None = None
    cpu_weight: int | None = None
    pids_current: int | None = None
    io_rbps_limit: int | None = None
    io_wbps_limit: int | None = None
    mock: bool = False


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
