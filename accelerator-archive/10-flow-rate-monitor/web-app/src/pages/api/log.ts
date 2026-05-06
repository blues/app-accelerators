/* eslint-disable no-console */
export const serverLogError = console.error;
export const serverLogInfo = console.info;

const DEFAULT = { serverLogError, serverLogInfo };
export default DEFAULT;
