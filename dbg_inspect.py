from backend.app.services.engine import inspect_container_details, create_container
res = create_container(
    name="dbg1",
    command="/bin/sh",
    memory=128,
    cpu=50,
    nice_value=5,
    cpuset="0-1",
    memory_high_mb=64,
    swap_limit_mb=128,
    restart_policy="always",
    drop_caps=True,
    seccomp_enabled=True,
)
print("create_res:", res)
details = inspect_container_details("dbg1")
print("details:", details)

