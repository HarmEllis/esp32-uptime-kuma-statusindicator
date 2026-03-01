import { mkdir, rm, writeFile } from "node:fs/promises";
import path from "node:path";

const REQUIRED_FILES = [
  "bootloader-esp32.bin",
  "partition-table-esp32.bin",
  "firmware-esp32.bin",
];

const MAX_RELEASES = Number.parseInt(
  process.env.MAX_FIRMWARE_RELEASES ?? "10",
  10
);

const token = process.env.GITHUB_TOKEN;
const repository = process.env.GITHUB_REPOSITORY;

if (!token) {
  throw new Error("GITHUB_TOKEN is required.");
}

if (!repository) {
  throw new Error("GITHUB_REPOSITORY is required.");
}

const outputDir = path.resolve("webapp/public/firmware");

function buildHeaders(accept) {
  return {
    Authorization: `Bearer ${token}`,
    Accept: accept,
    "X-GitHub-Api-Version": "2022-11-28",
  };
}

async function fetchJson(url) {
  const response = await fetch(url, {
    headers: buildHeaders("application/vnd.github+json"),
  });

  if (!response.ok) {
    throw new Error(`GitHub API request failed (${response.status}): ${url}`);
  }

  return response.json();
}

async function downloadAsset(assetApiUrl, destinationPath) {
  const response = await fetch(assetApiUrl, {
    headers: buildHeaders("application/octet-stream"),
  });

  if (!response.ok) {
    throw new Error(
      `Asset download failed (${response.status}): ${assetApiUrl}`
    );
  }

  const bytes = Buffer.from(await response.arrayBuffer());
  await writeFile(destinationPath, bytes);
}

const releasesUrl = `https://api.github.com/repos/${repository}/releases?per_page=${MAX_RELEASES}`;
const releases = await fetchJson(releasesUrl);

if (!Array.isArray(releases)) {
  throw new Error("Unexpected releases API response.");
}

await rm(outputDir, { recursive: true, force: true });
await mkdir(outputDir, { recursive: true });

const manifest = [];

for (const release of releases) {
  const tag = typeof release?.tag_name === "string" ? release.tag_name : "";
  if (!tag.startsWith("v")) {
    continue;
  }

  const assets = Array.isArray(release.assets) ? release.assets : [];
  const assetsByName = new Map();
  for (const asset of assets) {
    if (typeof asset?.name === "string" && typeof asset?.url === "string") {
      assetsByName.set(asset.name, asset);
    }
  }

  const missing = REQUIRED_FILES.filter((name) => !assetsByName.has(name));
  if (missing.length > 0) {
    console.log(`Skipping ${tag}: missing ${missing.join(", ")}`);
    continue;
  }

  const tagDir = path.join(outputDir, tag);
  await mkdir(tagDir, { recursive: true });

  for (const filename of REQUIRED_FILES) {
    const asset = assetsByName.get(filename);
    const destination = path.join(tagDir, filename);
    console.log(`Downloading ${tag}/${filename}`);
    await downloadAsset(asset.url, destination);
  }

  manifest.push({
    tag,
    url: typeof release.html_url === "string" ? release.html_url : "",
    files: Object.fromEntries(
      REQUIRED_FILES.map((filename) => [
        filename,
        `firmware/${tag}/${filename}`,
      ])
    ),
  });
}

if (manifest.length === 0) {
  throw new Error("No releases with required firmware assets were found.");
}

await writeFile(
  path.join(outputDir, "releases.json"),
  `${JSON.stringify(manifest, null, 2)}\n`,
  "utf8"
);

console.log(`Prepared ${manifest.length} release(s) for GitHub Pages.`);
