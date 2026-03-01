import { render } from "preact";
import { App } from "./app";

// Handle SPA redirect from 404.html (GitHub Pages fallback)
const params = new URLSearchParams(window.location.search);
const spaRedirect = params.get("__spa_redirect");
if (spaRedirect) {
  history.replaceState(null, "", spaRedirect);
}

render(<App />, document.getElementById("app")!);
