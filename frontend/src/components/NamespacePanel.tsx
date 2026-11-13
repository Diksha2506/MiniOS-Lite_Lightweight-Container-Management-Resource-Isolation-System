import React, { useEffect, useState } from 'react'
import { ShieldCheck, AlertCircle, RefreshCw, Layers } from 'lucide-react'
import { fetchNamespaces } from '../services/api'
import type { NamespaceInspectResponse } from '../types'

interface Props {
  containerName: string
  isOpen: boolean
  onClose: () => void
}

export default function NamespacePanel({ containerName, isOpen, onClose }: Props) {
  const [data, setData] = useState<NamespaceInspectResponse | null>(null)
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState<string | null>(null)

  const loadData = () => {
    if (!containerName) return
    setLoading(true)
    setError(null)
    fetchNamespaces(containerName)
      .then(res => setData(res))
      .catch(err => setError(err?.response?.data?.detail || 'Failed to inspect namespaces'))
      .finally(() => setLoading(false))
  }

  useEffect(() => {
    if (isOpen) {
      loadData()
    }
  }, [isOpen, containerName])

  if (!isOpen) return null

  return (
    <div className="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4">
      <div className="bg-white rounded-xl shadow-2xl max-w-2xl w-full overflow-hidden border border-gray-100 flex flex-col max-h-[85vh]">
        {/* Header */}
        <div className="px-6 py-4 border-b border-gray-100 flex items-center justify-between bg-gray-50/50">
          <div className="flex items-center gap-2">
            <Layers className="text-indigo-600" size={20} />
            <div>
              <h2 className="text-base font-bold text-gray-900">
                Namespace Isolation: <span className="font-mono text-indigo-700">{containerName}</span>
              </h2>
              <p className="text-xs text-gray-500">
                Linux kernel namespace boundaries &amp; inode identifiers
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
        <div className="p-6 overflow-y-auto space-y-4 flex-1">
          {data?.mock && (
            <div className="bg-amber-50 border border-amber-200 text-amber-800 px-3.5 py-2 rounded-lg text-xs flex items-center gap-2">
              <span className="font-semibold px-1.5 py-0.5 bg-amber-200 rounded text-[10px]">SIMULATED</span>
              Windows Mock Mode active: namespace inodes are simulated for demonstration.
            </div>
          )}

          {error && (
            <div className="bg-red-50 border border-red-200 text-red-700 p-3 rounded-lg text-sm flex items-center gap-2">
              <AlertCircle size={16} /> {error}
            </div>
          )}

          {loading ? (
            <div className="space-y-2 py-8">
              {[...Array(6)].map((_, i) => (
                <div key={i} className="h-8 bg-gray-100 rounded animate-pulse" />
              ))}
            </div>
          ) : data ? (
            <div className="border border-gray-100 rounded-lg overflow-hidden">
              <table className="w-full text-xs">
                <thead className="bg-gray-50 text-gray-500 uppercase font-semibold border-b border-gray-100">
                  <tr>
                    <th className="px-4 py-2.5 text-left">Namespace Type</th>
                    <th className="px-4 py-2.5 text-left">Status</th>
                    <th className="px-4 py-2.5 text-left">Inode ID</th>
                    <th className="px-4 py-2.5 text-left font-mono">Kernel Path</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-gray-50">
                  {data.namespaces.map(ns => (
                    <tr key={ns.type} className="hover:bg-gray-50/50 transition-colors">
                      <td className="px-4 py-2.5 font-semibold text-gray-800 uppercase flex items-center gap-1.5">
                        <span className="w-1.5 h-1.5 rounded-full bg-indigo-500"></span>
                        {ns.type}
                      </td>
                      <td className="px-4 py-2.5">
                        {ns.available ? (
                          <span className="inline-flex items-center gap-1 text-[11px] font-medium text-emerald-700 bg-emerald-50 px-2 py-0.5 rounded-full border border-emerald-200/60">
                            <ShieldCheck size={11} /> ISOLATED
                          </span>
                        ) : (
                          <span className="inline-flex items-center text-[11px] font-medium text-gray-500 bg-gray-100 px-2 py-0.5 rounded-full">
                            UNAVAILABLE
                          </span>
                        )}
                      </td>
                      <td className="px-4 py-2.5 font-mono text-gray-700 font-medium">
                        {ns.inode > 0 ? ns.inode : '—'}
                      </td>
                      <td className="px-4 py-2.5 font-mono text-gray-400 text-[11px] truncate max-w-xs">
                        {ns.path || '—'}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          ) : null}
        </div>

        {/* Footer */}
        <div className="px-6 py-3 border-t border-gray-100 bg-gray-50 flex items-center justify-between text-xs text-gray-500">
          <span>Container PID: <strong className="font-mono text-gray-800">{data?.pid || '—'}</strong></span>
          <button onClick={onClose} className="btn-ghost py-1 px-3 text-xs">
            Close
          </button>
        </div>
      </div>
    </div>
  )
}

