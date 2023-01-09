import axios, { AxiosResponse } from "axios";
import { AppNotification } from "../components/presentation/notifications";
import { services } from "../services/ServiceLocatorClient";

export async function removeNotification(notificationID: string) {
  const endpoint = services().getUrlManager().notifications(notificationID);
  const response: AxiosResponse = await axios.delete(endpoint);
  return response.status === 201;
}

export type AppNotifications = {
  readonly notifications: AppNotification[];
}

/**
 * Expands the raw notifications to presentation form.
 * @returns
 */
export async function presentNotifications() : Promise<AppNotifications> {
  const endpoint = services().getUrlManager().presentNotifications();
  const response: AxiosResponse = await axios.get(endpoint);
  return response.data;
}

/**
 * Retrieves the "raw" notifications, as they were added to the API.
 * @returns
 */
export async function fetchNotifications() {
  const endpoint = services().getUrlManager().notifications();
  const response: AxiosResponse = await axios.get(endpoint);
  return response.data;
}
