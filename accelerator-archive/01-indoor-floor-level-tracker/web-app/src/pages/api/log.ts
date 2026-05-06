/* eslint-disable no-console */
export const serverLogError = console.error;
export const serverLogInfo = console.info;

// message 50% (500/1000)
// message 40% (400/1000)
// etc.
export function serverLogProgress(
  message: string,
  total: number,
  current: number,
  updateIncrement = 100
) {
  if (current % updateIncrement === 0)
    serverLogInfo(
      `${message} ${Math.round((100 * current) / total)}% (${current}/${total})`
    );
}

const DEFAULT = { serverLogError, serverLogInfo, serverLogProgress };
export default DEFAULT;
