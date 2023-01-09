import { ErrorWithCause } from "pony-cause";

// Polyfill that wraps the JavaScript error object to support a cause property.
// When TypeScript officially supports the `new Error(message, { cause: e })`
// syntax we can just use that directly.
//
// See https://2ality.com/2021/06/error-cause.html
export function getError(message: string, options?: { cause: Error }) {
  return new ErrorWithCause(message, options);
}

// eslint-disable-next-line @typescript-eslint/naming-convention

export enum ERROR_CODES {
  UNAUTHORIZED = "UNAUTHORIZED",
  FORBIDDEN = "FORBIDDEN",
  DEVICE_NOT_FOUND = "DEVICE_NOT_FOUND",
  INTERNAL_ERROR = "INTERNAL_ERROR",
  DATABASE_NOT_RUNNING = "DATABASE_NOT_RUNNING",
  NO_PROJECT_ID = "NO_PROJECT_ID",
  DEVICE_NAME_CHANGE_FAILED = "DEVICE_NAME_CHANGE_FAILED",
  DEVICE_CONFIG_NOT_FOUND = "DEVICE_CONFIG_NOT_FOUND",
}

type ErrorResponse<E> = { err: E };
type NoErrorResponse = { err: undefined };
export type MayError<M, E> = (NoErrorResponse & M) | ErrorResponse<E>;

export function isError<E>(t: MayError<unknown, E>): t is ErrorResponse<E> {
  return t.err !== undefined;
}
