import * as BuiltinRenderers from "./builtin";
import * as CustomRenderers from "./custom";
import { OverridingRendererRegistry } from "./registry";

const registry = new OverridingRendererRegistry();

BuiltinRenderers.registerRenderers(registry);
CustomRenderers.registerRenderers(registry);

export default registry;
