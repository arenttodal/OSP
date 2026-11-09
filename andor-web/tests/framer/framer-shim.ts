// Stand-in for Framer's "framer" module so the code components can be rendered and tested
// outside Framer. Records each component's property controls for inspection by the tests.
export const ControlType = {
  Boolean: 'boolean', Number: 'number', String: 'string', Color: 'color', Enum: 'enum', File: 'file',
  Image: 'image', ResponsiveImage: 'responsiveimage', ComponentInstance: 'componentinstance', Array: 'array',
  Object: 'object', EventHandler: 'eventhandler', Transition: 'transition', Link: 'link', Font: 'font',
} as const;
export const registry = new Map<any, Record<string, any>>();
export function addPropertyControls(component: any, controls: Record<string, any>) {
  registry.set(component, controls);
  (globalThis as any).__framerControls ??= {};
  (globalThis as any).__framerControls[component.name] = controls;
}
