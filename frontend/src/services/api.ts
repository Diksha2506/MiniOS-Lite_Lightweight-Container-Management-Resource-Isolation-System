import axios from 'axios'
import type {
  Container,
  ContainerStats,
  SystemInfo,
  ApiResponse,
  CreateContainerPayload,
  NamespaceInspectResponse,
  NetworkInspectResponse,
} from '../types'

const api = axios.create({
  baseURL: 'http://localhost:8000',
  timeout: 10000,
})

export const fetchContainers = (): Promise<Container[]> =>
  api.get<Container[]>('/api/containers').then(r => r.data)

export const fetchSystem = (): Promise<SystemInfo> =>
  api.get<SystemInfo>('/api/system').then(r => r.data)

export const fetchHealth = () =>
  api.get('/api/health').then(r => r.data)

export const fetchStats = (name: string): Promise<ContainerStats> =>
  api.get<ContainerStats>(`/api/containers/${name}/stats`).then(r => r.data)

export const createContainer = (payload: CreateContainerPayload): Promise<ApiResponse> =>
  api.post<ApiResponse>('/api/containers', payload).then(r => r.data)

export const startContainer = (name: string): Promise<ApiResponse> =>
  api.post<ApiResponse>(`/api/containers/${name}/start`).then(r => r.data)

export const stopContainer = (name: string): Promise<ApiResponse> =>
  api.post<ApiResponse>(`/api/containers/${name}/stop`).then(r => r.data)

export const removeContainer = (name: string): Promise<ApiResponse> =>
  api.delete<ApiResponse>(`/api/containers/${name}`).then(r => r.data)

export const fetchNamespaces = (name: string): Promise<NamespaceInspectResponse> =>
  api.get<NamespaceInspectResponse>(`/api/containers/${name}/namespaces`).then(r => r.data)

export const fetchNetwork = (name: string): Promise<NetworkInspectResponse> =>
  api.get<NetworkInspectResponse>(`/api/containers/${name}/network`).then(r => r.data)

export const connectNetwork = (name: string): Promise<ApiResponse> =>
  api.post<ApiResponse>(`/api/containers/${name}/connect-network`).then(r => r.data)

export const disconnectNetwork = (name: string): Promise<ApiResponse> =>
  api.delete<ApiResponse>(`/api/containers/${name}/network`).then(r => r.data)
