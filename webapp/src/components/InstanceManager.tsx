import { useState, useEffect } from "preact/hooks";
import { RoutableProps } from "preact-router";
import { v4 as uuidv4 } from "uuid";
import { PageHeader } from "./PageHeader";
import { getInstances, addInstance, updateInstance, deleteInstance } from "../utils/api";
import type { UptimeInstance } from "../types/api";

interface EditState {
  uuid: string;
  name: string;
  url: string;
  apikey: string;
}

function emptyEdit(): EditState {
  return { uuid: uuidv4(), name: "", url: "", apikey: "" };
}

export function InstanceManager(_props: RoutableProps) {
  const [instances, setInstances] = useState<UptimeInstance[]>([]);
  const [editing, setEditing] = useState<{ id: number | null; state: EditState } | null>(null);
  const [error, setError] = useState("");
  const [success, setSuccess] = useState("");
  const [loading, setLoading] = useState(false);
  const [deleteConfirm, setDeleteConfirm] = useState<number | null>(null);

  const load = async () => {
    try {
      const { instances: list } = await getInstances();
      setInstances(list);
    } catch (e) {
      setError(e instanceof Error ? e.message : "Failed to load instances");
    }
  };

  useEffect(() => {
    load();
  }, []);

  const handleAdd = () => {
    setEditing({ id: null, state: emptyEdit() });
    setError("");
    setSuccess("");
  };

  const handleEdit = (inst: UptimeInstance) => {
    setEditing({ id: inst.id, state: { uuid: inst.uuid, name: inst.name, url: inst.url, apikey: inst.apikey } });
    setError("");
    setSuccess("");
  };

  const handleSave = async () => {
    if (!editing) return;
    const { id, state } = editing;

    if (!state.name.trim() || !state.url.trim()) {
      setError("Name and URL are required");
      return;
    }

    setLoading(true);
    setError("");
    try {
      if (id === null) {
        await addInstance({ uuid: state.uuid, name: state.name.trim(), url: state.url.trim(), apikey: state.apikey.trim() });
        setSuccess("Instance added");
      } else {
        await updateInstance(id, { uuid: state.uuid, name: state.name.trim(), url: state.url.trim(), apikey: state.apikey.trim() });
        setSuccess("Instance updated");
      }
      setEditing(null);
      await load();
    } catch (e) {
      setError(e instanceof Error ? e.message : "Failed to save instance");
    } finally {
      setLoading(false);
    }
  };

  const handleDelete = async (id: number) => {
    setLoading(true);
    setError("");
    try {
      await deleteInstance(id);
      setSuccess("Instance deleted");
      setDeleteConfirm(null);
      await load();
    } catch (e) {
      setError(e instanceof Error ? e.message : "Failed to delete instance");
    } finally {
      setLoading(false);
    }
  };

  const handleImport = () => {
    const input = document.createElement("input");
    input.type = "file";
    input.accept = ".json";
    input.onchange = async (e) => {
      const file = (e.target as HTMLInputElement).files?.[0];
      if (!file) return;
      try {
        const text = await file.text();
        const data = JSON.parse(text) as { instances?: Array<Record<string, string>> };
        if (!data.instances || !Array.isArray(data.instances)) {
          setError("Invalid config file: missing instances array");
          return;
        }
        setLoading(true);
        // Delete all existing instances (in reverse order)
        for (let i = instances.length - 1; i >= 0; i--) {
          await deleteInstance(i);
        }
        // Add each imported instance
        for (const raw of data.instances) {
          const uuid = raw.uuid ?? raw.id ?? uuidv4();
          const url = raw.url ?? raw.endpoint ?? "";
          await addInstance({ uuid, name: raw.name ?? "", url, apikey: raw.apikey ?? "" });
        }
        setSuccess(`Imported ${data.instances.length} instance(s)`);
        await load();
      } catch (e) {
        setError(e instanceof Error ? e.message : "Import failed");
      } finally {
        setLoading(false);
      }
    };
    input.click();
  };

  const handleExport = () => {
    const data = {
      instances: instances.map(({ uuid, name, url, apikey }) => ({ uuid, name, url, apikey })),
    };
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: "application/json" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = "uptimemon-instances.json";
    a.click();
  };

  return (
    <div style={{ padding: "2rem", maxWidth: "600px", margin: "0 auto" }}>
      <PageHeader title="Instances" />

      {/* Instance list */}
      {instances.length === 0 && !editing && (
        <div style={{ textAlign: "center", padding: "2rem", color: "#64748b" }}>
          <p>No instances configured yet.</p>
        </div>
      )}

      {instances.map((inst) => (
        <div
          key={inst.id}
          style={{ padding: "0.75rem 1rem", background: "#1e293b", borderRadius: "8px", marginBottom: "0.5rem", display: "flex", justifyContent: "space-between", alignItems: "center" }}
        >
          <div>
            <div style={{ fontWeight: "bold" }}>{inst.name}</div>
            <div style={{ color: "#64748b", fontSize: "0.8rem", fontFamily: "monospace" }}>{inst.url}</div>
          </div>
          <div style={{ display: "flex", gap: "0.4rem" }}>
            <button
              onClick={() => handleEdit(inst)}
              style={{ ...smallBtnStyle, background: "#374151" }}
            >
              Edit
            </button>
            {deleteConfirm === inst.id ? (
              <>
                <button
                  onClick={() => handleDelete(inst.id)}
                  disabled={loading}
                  style={{ ...smallBtnStyle, background: "#991b1b" }}
                >
                  Confirm
                </button>
                <button
                  onClick={() => setDeleteConfirm(null)}
                  style={{ ...smallBtnStyle, background: "#374151" }}
                >
                  Cancel
                </button>
              </>
            ) : (
              <button
                onClick={() => setDeleteConfirm(inst.id)}
                style={{ ...smallBtnStyle, background: "#7f1d1d", color: "#fca5a5" }}
              >
                Delete
              </button>
            )}
          </div>
        </div>
      ))}

      {/* Edit / Add form */}
      {editing && (
        <div style={{ padding: "1rem", background: "#1e293b", borderRadius: "8px", marginBottom: "1rem" }}>
          <h3 style={{ marginTop: 0, fontSize: "1rem" }}>{editing.id === null ? "Add Instance" : "Edit Instance"}</h3>
          <div style={{ display: "flex", flexDirection: "column", gap: "0.5rem" }}>
            <input
              type="text"
              placeholder="Name"
              value={editing.state.name}
              onInput={(e) => setEditing({ ...editing, state: { ...editing.state, name: (e.target as HTMLInputElement).value } })}
              style={inputStyle}
            />
            <input
              type="text"
              placeholder="URL (e.g. https://kuma.example.com)"
              value={editing.state.url}
              onInput={(e) => setEditing({ ...editing, state: { ...editing.state, url: (e.target as HTMLInputElement).value } })}
              style={inputStyle}
            />
            <input
              type="password"
              placeholder="API Key (optional)"
              value={editing.state.apikey}
              onInput={(e) => setEditing({ ...editing, state: { ...editing.state, apikey: (e.target as HTMLInputElement).value } })}
              style={inputStyle}
            />
            <div style={{ display: "flex", gap: "0.5rem" }}>
              <button
                onClick={handleSave}
                disabled={loading}
                style={{ ...btnStyle, background: loading ? "#374151" : "#22c55e", flex: 1 }}
              >
                {loading ? "Saving..." : "Save"}
              </button>
              <button
                onClick={() => setEditing(null)}
                style={{ ...btnStyle, background: "#374151" }}
              >
                Cancel
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Actions */}
      {!editing && (
        <div style={{ display: "flex", gap: "0.5rem", marginTop: "1rem", flexWrap: "wrap" }}>
          <button onClick={handleAdd} style={{ ...btnStyle, background: "#3b82f6" }}>
            + Add Instance
          </button>
          <button onClick={handleExport} style={{ ...btnStyle, background: "#374151" }}>
            Export
          </button>
          <button onClick={handleImport} style={{ ...btnStyle, background: "#374151" }}>
            Import
          </button>
        </div>
      )}

      {success && <p style={{ color: "#22c55e", fontSize: "0.85rem", marginTop: "1rem" }}>{success}</p>}
      {error && <p style={{ color: "#ef4444", fontSize: "0.85rem", marginTop: "0.5rem" }}>{error}</p>}
    </div>
  );
}

const inputStyle: Record<string, string> = {
  padding: "0.5rem",
  background: "#0f172a",
  color: "#e2e8f0",
  border: "1px solid #334155",
  borderRadius: "6px",
  fontSize: "0.9rem",
};

const btnStyle: Record<string, string> = {
  padding: "0.5rem 1rem",
  color: "white",
  border: "none",
  borderRadius: "6px",
  cursor: "pointer",
};

const smallBtnStyle: Record<string, string> = {
  padding: "0.3rem 0.6rem",
  color: "#94a3b8",
  border: "none",
  borderRadius: "6px",
  cursor: "pointer",
  fontSize: "0.8rem",
};
