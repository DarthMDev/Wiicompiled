import { resolve } from "node:path";
import { pathToFileURL } from "node:url";

const modulePath = process.argv[2];
if (!modulePath) {
  throw new Error("expected the generated Emscripten module path");
}

const { default: createMkwWebPublicRuntimeTests } = await import(
  pathToFileURL(resolve(modulePath)).href,
);
const runtime = await createMkwWebPublicRuntimeTests();
const abiVersion = runtime._MkwWebPublicRuntimeAbiVersion();
if (abiVersion !== 1) {
  throw new Error(`expected ABI version 1, got ${abiVersion}`);
}

console.log(`Web public runtime tests passed (ABI ${abiVersion}).`);
