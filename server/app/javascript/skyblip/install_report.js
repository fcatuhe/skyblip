// TODO: fc 13nov26 troubleshooting for #278, retired with #294 once Bluetooth updates land in the field

const CHECK_CAUSES = { damaged: 'slot_damaged', unreadable: 'slot_unreadable', undigested: 'slot_undigested' };
const FAILED_IMAGES = new Set(['reverted', 'refused']);

export const INSTALL_CAUSES = ['slot_damaged', 'slot_unreadable', 'slot_undigested', 'swap_not_marked', 'bootloader_refused', 'probation_failed', 'no_record'];

export function installReportOf(reply) {
  if (reply.check === undefined) return null;
  return {
    check: reply.check,
    swap: reply.swap,
    requestError: reply.request_error ?? 0,
    boots: reply.boots ?? 0,
    probationBoots: reply.probation_boots ?? 0,
    proof: reply.proof,
    reset: reply.reset,
  };
}

export function installCause({ image, report, running, attempted }) {
  if (report && FAILED_IMAGES.has(image?.state)) {
    if (CHECK_CAUSES[report.check]) return CHECK_CAUSES[report.check];
    if (report.requestError !== 0 || report.swap !== 'test') return 'swap_not_marked';
    return report.probationBoots === 0 ? 'bootloader_refused' : 'probation_failed';
  }
  const cameBackOld = Boolean(attempted && running && running !== attempted);
  return cameBackOld && image?.state === 'confirmed' ? 'no_record' : null;
}

export function reportShown({ image, cause }) {
  return Boolean(cause) || FAILED_IMAGES.has(image?.state);
}
