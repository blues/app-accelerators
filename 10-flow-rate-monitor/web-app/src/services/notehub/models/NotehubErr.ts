interface NotehubErr {
  err: string;
  code?: number;
  status?: string;
  request?: string;
  details?: object;
  debug?: string;
}

export default NotehubErr;
