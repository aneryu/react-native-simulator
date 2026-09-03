RN$SimulatorWorkload.ready();

const uim = globalThis.nativeFabricUIManager;
if (!uim || typeof uim.createNode !== 'function') {
  throw new Error('nativeFabricUIManager.createNode is required');
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

function yogaProps(width, height) {
  return {
    width: width,
    height: height,
    alignSelf: 'flex-start',
    flexGrow: 0,
    flexShrink: 0,
  };
}

const probeHandle = {tag: 100};
let probe = uim.createNode(
  100,
  'RNSFabricProbeView',
  SURFACE,
  yogaProps(120, 40),
  probeHandle,
);
uim.completeRoot(SURFACE, childSet([probe]));

afterCommit(function () {
  const first = uim.getRelativeLayoutMetrics(probe, probe);
  if (first.width !== 120) {
    throw new Error('expected Yoga width 120, got ' + first.width);
  }

  probe = uim.cloneNodeWithNewProps(probe, yogaProps(180, 40));
  uim.completeRoot(SURFACE, childSet([probe]));

  afterCommit(function () {
    const updated = uim.getRelativeLayoutMetrics(probe, probe);
    if (updated.width !== 180) {
      throw new Error('expected Yoga width 180, got ' + updated.width);
    }

    uim.dispatchCommand(probe, 'setNativeValue', [42]);
    uim.dispatchCommand(probe, 'notARealCommand', []);

    afterCommit(function () {
      const probeEvent = events.find(function (event) {
        return event.type === 'topProbeEvent' || event.type === 'probeEvent';
      });
      if (!probeEvent || !probeEvent.payload || probeEvent.payload.value !== 42) {
        throw new Error(
          'typed probe event missing: ' + JSON.stringify(events));
      }
      if (globalThis.__rnsProbeExecutorPosted !== true) {
        throw new Error('foreign-thread executor post was not delivered');
      }
      const mounts = globalThis.__rnsProbeMounts();
      if (mounts.mounted < 1) {
        throw new Error('probe mount handler was not invoked');
      }

      uim.completeRoot(SURFACE, childSet([]));
      afterCommit(function () {
        const afterRemove = globalThis.__rnsProbeMounts();
        if (afterRemove.unmounted < 1) {
          throw new Error('probe unmount handler was not invoked');
        }
        globalThis.RN$SimulatorWorkloadResult = {
          iterations: 1,
          checksum: 21,
          yogaWidth: updated.width,
          eventValue: probeEvent.payload.value,
        };
        RN$SimulatorWorkload.complete();
      });
    });
  });
});
