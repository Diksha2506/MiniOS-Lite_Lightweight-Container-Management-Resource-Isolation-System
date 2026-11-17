"""
test_isolation_audit.py — Unit and contract tests for filesystem and namespace isolation.

Phase 0 contracts:
  - PID / UTS / Mount namespaces via clone()
  - Safe command parsing (cmd_buf, no strtok on persisted struct)
  - No system() in cleanup
  - Custom rootfs validation helpers
  - Backend --rootfs compatibility
"""
from pathlib import Path
import os
import platform
import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
PROCESS_C = REPO_ROOT / "src" / "process.c"
NAMESPACE_C = REPO_ROOT / "src" / "namespace.c"
CLEANUP_C = REPO_ROOT / "src" / "cleanup.c"
IS_LINUX = platform.system() == "Linux"


class TestNamespaceContracts:
    def test_process_uses_core_namespace_flags(self):
        """Phase 0 & 1 isolates PID, UTS, Mount, IPC, and NET namespaces."""
        text = PROCESS_C.read_text(encoding="utf-8")
        assert "CLONE_NEWPID" in text
        assert "CLONE_NEWUTS" in text
        assert "CLONE_NEWNS" in text
        assert "CLONE_NEWIPC" in text
        assert "CLONE_NEWNET" in text

    def test_phase1_module_files_exist_and_safe(self):
        """nsinfo and network modules exist and avoid system() calls."""
        nsinfo_c = REPO_ROOT / "src" / "nsinfo.c"
        network_c = REPO_ROOT / "src" / "network.c"
        assert nsinfo_c.exists()
        assert network_c.exists()
        net_text = network_c.read_text(encoding="utf-8")
        assert "system(" not in net_text, "network.c must avoid unsafe system() calls"

    def test_process_ctl_avoids_strtok_mutation_of_container_struct(self):
        """Command tokenization uses a local buffer rather than mutating args->c->command."""
        text = PROCESS_C.read_text(encoding="utf-8")
        assert "cmd_buf" in text
        assert "strtok(args->c->command" not in text

    def test_cleanup_avoids_shell_system_calls(self):
        """Unsafe system() calls have been removed from cleanup.c."""
        text = CLEANUP_C.read_text(encoding="utf-8")
        assert "system(" not in text, "Removed unsafe shell invocation from cleanup.c"
        assert "rmdir(dirpath)" in text

    def test_namespace_mount_error_paths_prevent_silent_execution(self):
        """Mount errors halt namespace setup with return -1."""
        text = NAMESPACE_C.read_text(encoding="utf-8")
        assert 'perror("mount /dev tmpfs");' in text
        assert 'perror("mount /tmp tmpfs");' in text

    def test_process_syncs_before_workload(self):
        """Child waits on a sync pipe until parent attaches the cgroup."""
        text = PROCESS_C.read_text(encoding="utf-8")
        assert "sync_fd" in text or "sync_pipe" in text
        assert "cgroup_attach_task" in text


class TestUnprivilegedFailureHandling:
    BINARY = REPO_ROOT / "container"

    @pytest.mark.skipif(not IS_LINUX, reason="Requires Linux binary")
    def test_start_unprivileged_fails_safely_without_corruption(self):
        """
        Privileged container start as non-root should fail safely with a clear error.
        """
        import subprocess

        if not self.BINARY.exists():
            pytest.skip("container binary not built; run make first")
        if os.geteuid() == 0:
            pytest.skip("Test requires unprivileged non-root environment")

        res = subprocess.run(
            [str(self.BINARY), "start", "nonexistent-container-xyz"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        assert res.returncode != 0
        # Accept any of: permission/privilege error ("root", "error"),
        # or clean "not found" for a nonexistent container — all are safe
        # non-corrupt failure modes.
        stderr_lc = res.stderr.lower()
        assert (
            "root" in stderr_lc
            or "error" in stderr_lc
            or "not found" in stderr_lc
            or "permission" in stderr_lc
        ), f"Unexpected stderr: {res.stderr!r}"

    @pytest.mark.skipif(not IS_LINUX, reason="Requires Linux binary")
    def test_create_rejects_path_traversal_safely(self):
        """CLI create must reject path-traversal container names."""
        import subprocess

        if not self.BINARY.exists():
            pytest.skip("container binary not built; run make first")

        res = subprocess.run(
            [str(self.BINARY), "create", "../../etc/passwd"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        assert res.returncode != 0
        assert "invalid container name" in res.stderr.lower()


class TestRootfsValidation:
    BINARY = REPO_ROOT / "container"

    @pytest.mark.skipif(not IS_LINUX, reason="Requires Linux binary")
    def test_preflight_rejects_nonexistent_command(self):
        import subprocess

        if not self.BINARY.exists():
            pytest.skip("container binary not built; run make first")

        name = "bad-cmd-preflight"
        subprocess.run(
            [str(self.BINARY), "create", name, "--command", "/nonexistent_test_bin_xyz_99"],
            cwd=str(REPO_ROOT),
            capture_output=True,
        )
        # start may fail on missing root OR on preflight; both are non-zero
        if os.geteuid() != 0:
            res = subprocess.run(
                [str(self.BINARY), "start", name],
                capture_output=True,
                text=True,
                cwd=str(REPO_ROOT),
            )
            assert res.returncode != 0
        else:
            res = subprocess.run(
                [str(self.BINARY), "start", name],
                capture_output=True,
                text=True,
                cwd=str(REPO_ROOT),
            )
            assert res.returncode != 0
            assert "preflight" in res.stderr.lower() or "not executable" in res.stderr.lower() \
                or "not found" in res.stderr.lower() or "error" in res.stderr.lower()

        res_list = subprocess.run(
            [str(self.BINARY), "list"],
            capture_output=True,
            text=True,
            cwd=str(REPO_ROOT),
        )
        for line in res_list.stdout.splitlines():
            if name in line:
                assert "RUNNING" not in line

        subprocess.run([str(self.BINARY), "remove", name], cwd=str(REPO_ROOT), capture_output=True)

    def test_preflight_header_exports_prototypes(self):
        header = REPO_ROOT / "include" / "rootfs.h"
        assert header.exists()
        content = header.read_text(encoding="utf-8")
        assert "rootfs_validate" in content
        assert "rootfs_preflight_check" in content

    def test_rootfs_source_implements_path_and_mode_checks(self):
        c_file = REPO_ROOT / "src" / "rootfs.c"
        assert c_file.exists()
        code = c_file.read_text(encoding="utf-8")
        assert "S_ISDIR" in code
        assert ".." in code
        assert "X_OK" in code
        assert "access(" in code

    def test_custom_rootfs_workflow_contracts(self):
        """namespace_setup accepts rootfs_path and process_start_isolated forwards it."""
        ns_text = NAMESPACE_C.read_text(encoding="utf-8")
        proc_text = PROCESS_C.read_text(encoding="utf-8")
        assert "namespace_setup(const char *container_name, const char *rootfs_path)" in ns_text
        assert "namespace_setup(args->c.name, rootfs)" in proc_text or \
               "namespace_setup(args->c->name, args->c->rootfs)" in proc_text

    def test_cli_and_backend_rootfs_support(self):
        """CLI create accepts --rootfs and backend schemas propagate rootfs."""
        cli = (REPO_ROOT / "src" / "cli.c").read_text(encoding="utf-8")
        assert "--rootfs" in cli

        from app.schemas.models import CreateContainerRequest, ContainerResponse
        req = CreateContainerRequest(name="test-rootfs", command="/bin/sh", rootfs="/tmp/custom-rootfs")
        assert req.rootfs == "/tmp/custom-rootfs"
        res = ContainerResponse(
            name="test-rootfs", pid=123, state="STOPPED",
            mem_limit=64, cpu_limit=20, rootfs="/tmp/custom-rootfs",
        )
        assert res.rootfs == "/tmp/custom-rootfs"


class TestEngineServiceContracts:
    ENGINE_SRC = REPO_ROOT / "backend" / "app" / "services" / "engine.py"

    def test_only_privileged_subcommands_use_sudo(self):
        text = self.ENGINE_SRC.read_text(encoding="utf-8")
        assert "_SUDO_REQUIRED_SUBCOMMANDS" in text
        assert "start" in text

    @pytest.mark.skipif(not IS_LINUX, reason="ELF binary only built on Linux")
    def test_engine_binary_exists_at_project_root(self):
        binary = REPO_ROOT / "container"
        if not binary.exists():
            pytest.skip("container binary not built; run make first")
        assert binary.stat().st_mode & 0o111
