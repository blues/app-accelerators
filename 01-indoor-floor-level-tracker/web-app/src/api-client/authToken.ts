/* eslint-disable import/prefer-default-export */
import axios, { AxiosResponse } from "axios";
import { services } from "../services/ServiceLocatorClient";

export async function handleAuthToken() {
  const endpoint = services().getUrlManager().handleAuthToken();
  const response: AxiosResponse = await axios.get(endpoint);
  return response.status === 200;
}
