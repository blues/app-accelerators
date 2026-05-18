/* eslint-disable import/prefer-default-export */
import axios from "axios";
import { services } from "../services/ServiceLocatorClient";

export async function clearAlarms() {
  const endpoint = services().getUrlManager().clearAlarms();
  await axios.post<object>(endpoint);
}
