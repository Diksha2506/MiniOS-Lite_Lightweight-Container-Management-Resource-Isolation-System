from fastapi import APIRouter, HTTPException
from ..services.engine import (
    list_containers,
    create_container,
    start_container,
    stop_container,
    remove_container,
    get_stats,
    inspect_namespaces,
    inspect_network,
    connect_network,
    disconnect_network,
    inspect_container_details,
    get_extended_stats,
)
from ..schemas.models import (
    CreateContainerRequest,
    ContainerResponse,
    ContainerInspectResponse,
    PhaseStatsResponse,
    StatsResponse,
    ApiResponse,
    NamespaceInspectResponse,
    NetworkInspectResponse,
)

router = APIRouter(prefix="/api/containers", tags=["containers"])


@router.get("", response_model=list[ContainerResponse])
def api_list():
    return list_containers()


@router.post("", response_model=ApiResponse)
def api_create(body: CreateContainerRequest):
    res = create_container(
        name=body.name,
        command=body.command,
        memory=body.memory_mb,
        cpu=body.cpu_pct,
        rootfs=body.rootfs,
        nice_value=body.nice_value,
        cpuset=body.cpuset,
        memory_high_mb=body.memory_high_mb,
        swap_limit_mb=body.swap_limit_mb,
        restart_policy=body.restart_policy,
        drop_caps=body.drop_caps,
        seccomp_enabled=body.seccomp_enabled,
        io_rbps_limit=body.io_rbps_limit,
        io_wbps_limit=body.io_wbps_limit,
    )
    if not res["ok"]:
        raise HTTPException(status_code=400, detail=res["stderr"] or res["stdout"])
    return ApiResponse(ok=True, message=res["stdout"].strip())


@router.get("/{name}/inspect", response_model=ContainerInspectResponse)
def api_inspect(name: str):
    details = inspect_container_details(name)
    if not details:
        raise HTTPException(status_code=404, detail=f"Container '{name}' not found.")
    return ContainerInspectResponse(**details)


@router.get("/{name}/extended-stats", response_model=PhaseStatsResponse)
def api_extended_stats(name: str):
    containers = list_containers()
    exists = any(c["name"] == name for c in containers)
    if not exists and not inspect_container_details(name):
        raise HTTPException(status_code=404, detail=f"Container '{name}' not found.")
    data = get_extended_stats(name)
    return PhaseStatsResponse(**data)


@router.get("/{name}/namespaces", response_model=NamespaceInspectResponse)
def api_namespaces(name: str):
    data = inspect_namespaces(name)
    if "error" in data and not data.get("namespaces"):
        raise HTTPException(status_code=400, detail=data["error"])
    return NamespaceInspectResponse(**data)


@router.get("/{name}/network", response_model=NetworkInspectResponse)
def api_network(name: str):
    data = inspect_network(name)
    if "error" in data and not data.get("ip_address") and not data.get("mock"):
        raise HTTPException(status_code=400, detail=data["error"])
    return NetworkInspectResponse(**data)


@router.post("/{name}/connect-network", response_model=ApiResponse)
def api_connect_network(name: str):
    res = connect_network(name)
    if not res.get("ok"):
        raise HTTPException(status_code=400, detail=res.get("message", "Failed to connect network"))
    return ApiResponse(ok=True, message=res["message"])


@router.delete("/{name}/network", response_model=ApiResponse)
def api_disconnect_network(name: str):
    res = disconnect_network(name)
    if not res.get("ok"):
        raise HTTPException(status_code=400, detail=res.get("message", "Failed to disconnect network"))
    return ApiResponse(ok=True, message=res["message"])


@router.get("/{name}/stats", response_model=StatsResponse)
def api_stats(name: str):
    data = get_stats(name)
    return StatsResponse(name=name, **data)


@router.get("/{name}", response_model=ContainerResponse)
def api_get(name: str):
    containers = list_containers()
    for c in containers:
        if c["name"] == name:
            return c
    raise HTTPException(status_code=404, detail=f"Container '{name}' not found.")


@router.post("/{name}/start", response_model=ApiResponse)
def api_start(name: str):
    res = start_container(name)
    if not res["ok"]:
        raise HTTPException(status_code=400, detail=res["stderr"] or res["stdout"])
    return ApiResponse(ok=True, message=res["stdout"].strip())


@router.post("/{name}/stop", response_model=ApiResponse)
def api_stop(name: str):
    res = stop_container(name)
    if not res["ok"]:
        raise HTTPException(status_code=400, detail=res["stderr"] or res["stdout"])
    return ApiResponse(ok=True, message=res["stdout"].strip())


@router.delete("/{name}", response_model=ApiResponse)
def api_remove(name: str):
    res = remove_container(name)
    if not res["ok"]:
        raise HTTPException(status_code=400, detail=res["stderr"] or res["stdout"])
    return ApiResponse(ok=True, message=res["stdout"].strip())


import asyncio
import os
from fastapi import WebSocket, WebSocketDisconnect

@router.websocket("/{name}/logs")
async def websocket_logs(websocket: WebSocket, name: str):
    await websocket.accept()
    
    # Prevent path traversal
    if not name or "/" in name or ".." in name or not name.replace("-", "").replace("_", "").isalnum():
        await websocket.send_text("Error: Invalid container name.\n")
        await websocket.close()
        return

    log_path = f"/tmp/containers/{name}.log"
    
    try:
        # Mock mode fallback for Windows/Mac
        if os.name != 'posix':
            for i in range(10):
                await websocket.send_text(f"[Mock Log] Container {name} is running - tick {i}\n")
                await asyncio.sleep(2)
            await websocket.close()
            return

        # Real Linux mode: tail the file
        if not os.path.exists(log_path):
            await websocket.send_text(f"Waiting for log file: {log_path}...\n")
            while not os.path.exists(log_path):
                await asyncio.sleep(0.5)

        with open(log_path, "r") as f:
            while True:
                line = f.readline()
                if not line:
                    await asyncio.sleep(0.2)
                    continue
                await websocket.send_text(line)

    except WebSocketDisconnect:
        pass
    except Exception as e:
        await websocket.send_text(f"Error reading logs: {str(e)}\n")
