import { route } from "preact-router";

/** Base path from Vite config (without trailing slash) */
export const BASE = import.meta.env.BASE_URL.replace(/\/$/, "");

/** Navigate to an app route, automatically prepending the base path */
export function nav(path: string): void {
  route(`${BASE}${path}`);
}
