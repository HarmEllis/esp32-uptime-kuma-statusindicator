import { nav } from "../utils/nav";

interface PageHeaderProps {
  title: string;
  backTo?: string;
}

export function PageHeader({ title, backTo = "/" }: PageHeaderProps) {
  return (
    <div
      style={{
        display: "flex",
        alignItems: "center",
        gap: "0.75rem",
        marginBottom: "1.25rem",
      }}
    >
      <button
        onClick={() => nav(backTo)}
        style={{
          background: "none",
          border: "none",
          color: "#94a3b8",
          cursor: "pointer",
          fontSize: "1.2rem",
          padding: "0.25rem",
          lineHeight: 1,
        }}
        aria-label="Go back"
      >
        &larr;
      </button>
      <h2 style={{ margin: 0 }}>{title}</h2>
    </div>
  );
}
