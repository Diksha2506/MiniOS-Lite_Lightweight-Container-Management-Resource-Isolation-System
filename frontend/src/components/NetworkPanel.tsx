import React, { useEffect, useState } from 'react'
import { Network, RefreshCw, AlertCircle, Wifi, Globe, Server, ArrowRightLeft } from 'lucide-react'
import { fetchNetwork, connectNetwork, disconnectNetwork } from '../services/api'
import type { NetworkInspectResponse } from '../types'
import toast from 'react-hot-toast'

interface Props {
  containerName: string
  isOpen: boolean
  onClose: () => void
}

export default function NetworkPanel({ containerName, isOpen, onClose }: Props) {
  const [data, setData] = useState<NetworkInspectResponse | null>(null)
  const [loading, setLoading] = useState(false)
  const [actionBusy, setActionBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)

  const loadData = () => {
    if (!containerName) return
    setLoading(true)
    setError(null)
    fetchNetwork(containerName)
      .then(res => setData(res))
      .catch(err => setError(err?.response?.data?.detail || 'Failed to inspect network'))
      .finally(() => setLoading(false))
  }

  useEffect(() => {
    if (isOpen) {
      loadData()
    }
  }, [isOpen, containerName])

  const handleConnect = async () => {
    setActionBusy(true)
    try {
      await connectNetwork(containerName)
      toast.success('Connected to bridge network')
      loadData()
    } catch (e: any) {
      toast.error(e?.response?.data?.detail || 'Connection failed')
    } finally {
      setActionBusy(false)
    }
  }

  const handleDisconnect = async () => {
    setActionBusy(true)
    try {
      await disconnectNetwork(containerName)
      toast.success('Disconnected from bridge network')
      loadData()
    } catch (e: any) {
      toast.error(e?.response?.data?.detail || 'Disconnection failed')
    } finally {
      setActionBusy(false)
    }
  }

  if (!isOpen) return null

  return (
    <div className="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4">
      <div className="bg-white rounded-xl shadow-2xl max-w-2xl w-full overflow-hidden border border-gray-100 flex flex-col max-h-[85vh]">
        {/* Header */}
        <div className="px-6 py-4 border-b border-gray-100 flex items-center justify-between bg-gray-50/50">
          <div className="flex items-center gap-2">
            <Network className="text-cyan-600" size={20} />
            <div>
              <h2 className="text-base font-bold text-gray-900">
                Bridge Networking: <span className="font-mono text-cyan-700">{containerName}</span>
              </h2>
              <p className="text-xs text-gray-500">
                veth pair, IP assignment &amp; Linux bridge topology
              </p>
            </div>
          </div>
          <div className="flex items-center gap-2">
            <button
              onClick={loadData}
              disabled={loading}
              className="p-1.5 text-gray-400 hover:text-gray-600 rounded-lg hover:bg-gray-100 transition-colors"
              title="Refresh"
            >
              <RefreshCw size={15} className={loading ? 'animate-spin' : ''} />
            </button>
            <button
              onClick={onClose}
              className="text-gray-400 hover:text-gray-600 font-bold px-2 py-1 text-sm rounded-lg hover:bg-gray-100"
            >
              ✕
            </button>
          </div>
        </div>

        {/* Content */}
        <div className="p-6 overflow-y-auto space-y-5 flex-1">
          {data?.mock && (
            <div className="bg-amber-50 border border-amber-200 text-amber-800 px-3.5 py-2 rounded-lg text-xs flex items-center gap-2">
              <span className="font-semibold px-1.5 py-0.5 bg-amber-200 rounded text-[10px]">SIMULATED</span>
              Windows Mock Mode: networking is simulated. Kernel veth &amp; bridge active only on Linux/WSL2.
            </div>
          )}

          {error && (
            <div className="bg-red-50 border border-red-200 text-red-700 p-3 rounded-lg text-sm flex items-center gap-2">
              <AlertCircle size={16} /> {error}
            </div>
          )}

          {loading ? (
            <div className="space-y-3 py-6">
              {[...Array(4)].map((_, i) => (
                <div key={i} className="h-10 bg-gray-100 rounded animate-pulse" />
              ))}
            </div>
          ) : data ? (
            <>
              {/* Visual Network Topology */}
              <div className="p-4 bg-slate-900 rounded-xl text-white">
                <div className="text-[11px] font-semibold text-slate-400 tracking-wider uppercase mb-3">
                  Network Topology
                </div>
                <div className="flex items-center justify-between gap-2 px-2 py-3 bg-slate-800/80 rounded-lg">
                  {/* Host Node */}
                  <div className="flex flex-col items-center gap-1 min-w-[80px]">
                    <Server size={22} className="text-slate-300" />
                    <span className="text-xs font-semibold">Host</span>
                    <span className="text-[10px] text-slate-400 font-mono">172.20.0.1</span>
                  </div>

                  {/* Veth Link 1 */}
                  <div className="flex-1 flex flex-col items-center">
                    <span className="text-[10px] font-mono text-cyan-400">
                      {data.host_interface || 'veth'}
                    </span>
                    <div className="w-full h-0.5 bg-cyan-500/60 my-1 relative">
                      <ArrowRightLeft size={10} className="absolute inset-0 m-auto text-cyan-300" />
                    </div>
                    <span className="text-[9px] text-slate-400">host end</span>
                  </div>

                  {/* Bridge Node */}
                  <div className="flex flex-col items-center gap-1 min-w-[90px] p-2 bg-slate-700/60 rounded-md border border-slate-600">
                    <Globe size={20} className="text-cyan-400" />
                    <span className="text-xs font-semibold text-cyan-200 font-mono">
                      {data.bridge || 'minios-br0'}
                    </span>
                    <span className="text-[10px] text-slate-300 font-mono">172.20.0.0/24</span>
                  </div>

                  {/* Veth Link 2 */}
                  <div className="flex-1 flex flex-col items-center">
                    <span className="text-[10px] font-mono text-emerald-400">
                      {data.container_interface || 'eth0'}
                    </span>
                    <div className="w-full h-0.5 bg-emerald-500/60 my-1 relative">
                      <ArrowRightLeft size={10} className="absolute inset-0 m-auto text-emerald-300" />
                    </div>
                    <span className="text-[9px] text-slate-400">container end</span>
                  </div>

                  {/* Container Node */}
                  <div className="flex flex-col items-center gap-1 min-w-[80px]">
                    <Wifi size={22} className={data.connected ? 'text-emerald-400' : 'text-slate-500'} />
                    <span className="text-xs font-semibold truncate max-w-[90px]">
                      {data.container}
                    </span>
                    <span className="text-[10px] text-emerald-300 font-mono">
                      {data.ip_address || '—'}
                    </span>
                  </div>
                </div>
              </div>

              {/* Details Grid */}
              <div className="grid grid-cols-2 gap-3 text-xs">
                <div className="p-3 bg-gray-50 rounded-lg border border-gray-100">
                  <div className="text-gray-400 font-medium mb-1">Connection Status</div>
                  <div className="font-semibold flex items-center gap-1.5">
                    {data.connected ? (
                      <span className="text-emerald-700 bg-emerald-100 px-2 py-0.5 rounded-full font-mono text-[11px]">
                        ● CONNECTED
                      </span>
                    ) : (
                      <span className="text-gray-600 bg-gray-200 px-2 py-0.5 rounded-full font-mono text-[11px]">
                        ○ DISCONNECTED
                      </span>
                    )}
                  </div>
                </div>

                <div className="p-3 bg-gray-50 rounded-lg border border-gray-100">
                  <div className="text-gray-400 font-medium mb-1">IP Address</div>
                  <div className="font-mono font-semibold text-gray-800 text-sm">
                    {data.ip_address || '—'}
                  </div>
                </div>

                <div className="p-3 bg-gray-50 rounded-lg border border-gray-100">
                  <div className="text-gray-400 font-medium mb-1">Gateway (Host Bridge)</div>
                  <div className="font-mono font-medium text-gray-700">
                    {data.gateway || '—'}
                  </div>
                </div>

                <div className="p-3 bg-gray-50 rounded-lg border border-gray-100">
                  <div className="text-gray-400 font-medium mb-1">Bridge Device</div>
                  <div className="font-mono font-medium text-gray-700">
                    {data.bridge || '—'}
                  </div>
                </div>

                <div className="p-3 bg-gray-50 rounded-lg border border-gray-100">
                  <div className="text-gray-400 font-medium mb-1">Host veth Interface</div>
                  <div className="font-mono font-medium text-gray-700">
                    {data.host_interface || '—'}
                  </div>
                </div>

                <div className="p-3 bg-gray-50 rounded-lg border border-gray-100">
                  <div className="text-gray-400 font-medium mb-1">Container Interface</div>
                  <div className="font-mono font-medium text-gray-700">
                    {data.container_interface || 'eth0'}
                  </div>
                </div>
              </div>
            </>
          ) : null}
        </div>

        {/* Footer */}
        <div className="px-6 py-3 border-t border-gray-100 bg-gray-50 flex items-center justify-between text-xs">
          <div className="flex gap-2">
            {!data?.connected ? (
              <button
                disabled={actionBusy}
                onClick={handleConnect}
                className="btn bg-cyan-50 text-cyan-700 hover:bg-cyan-100 text-xs px-2.5 py-1"
              >
                Connect Network
              </button>
            ) : (
              <button
                disabled={actionBusy}
                onClick={handleDisconnect}
                className="btn bg-gray-100 text-gray-600 hover:bg-gray-200 text-xs px-2.5 py-1"
              >
                Disconnect
              </button>
            )}
          </div>
          <button onClick={onClose} className="btn-ghost py-1 px-3 text-xs">
            Close
          </button>
        </div>
      </div>
    </div>
  )
}

