RN$SimulatorWorkload.ready();

const uim = globalThis.nativeFabricUIManager;
if (!uim || typeof uim.createNode !== 'function') {
  throw new Error('nativeFabricUIManager.createNode is required');
}
if (!globalThis.__nativeComponentRegistry__hasComponent('RNCSafeAreaProvider')) {
  throw new Error('RNCSafeAreaProvider must be registered by safe-area');
}

const SURFACE = 21;
const events = [];
uim.registerEventHandler(function (handle, type, payload) {
  events.push({type: String(type), payload: payload});
});

function childSet(nodes) {
  const set = uim.createChildSet();
  for (const node of nodes) {
    uim.appendChildToSet(set, node);
  }
  return set;
}

function afterCommit(fn) {
  setTimeout(fn, 0);
}

const provider = uim.createNode(
  100,
  'RNCSafeAreaProvider',
  SURFACE,
  {width: 300, height: 80, alignSelf: 'flex-start', flexGrow: 0, flexShrink: 0},
  {tag: 100},
);
uim.completeRoot(SURFACE, childSet([provider]));

afterCommit(function () {
  const layout = uim.getRelativeLayoutMetrics(provider, provider);
  const insets = events.filter(function (event) {
    return event.type === 'topInsetsChange' || event.type === 'insetsChange';
  });
  if (insets.length < 1) {
    throw new Error(
      'expected topInsetsChange, got ' + JSON.stringify(events));
  }
  const payload = insets[0].payload;
  if (!payload || !payload.frame || !payload.insets) {
    throw new Error('topInsetsChange payload missing frame/insets: ' +
      JSON.stringify(payload));
  }
  if (payload.insets.top !== 0 || payload.insets.right !== 0 ||
      payload.insets.bottom !== 0 || payload.insets.left !== 0) {
    throw new Error('v1 insets must be zero: ' + JSON.stringify(payload.insets));
  }
  if (payload.frame.width !== layout.width ||
      payload.frame.height !== layout.height) {
    throw new Error(
      'topInsetsChange frame must match committed layout: payload=' +
      JSON.stringify(payload.frame) + ' layout=' + JSON.stringify(layout));
  }
  globalThis.RN$SimulatorWorkloadResult = {
    iterations: 1,
    checksum: insets.length,
  };
  RN$SimulatorWorkload.complete();
});
