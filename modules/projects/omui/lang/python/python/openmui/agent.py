"""The agent's client (``openmui_agent``), also as ``openmui.agent``: drive
any running openmui application through openmui.tree@0.1."""

from openmui_agent import Agent, AgentError, connect, default_directory, endpoints

__all__ = ["Agent", "AgentError", "connect", "default_directory", "endpoints"]
