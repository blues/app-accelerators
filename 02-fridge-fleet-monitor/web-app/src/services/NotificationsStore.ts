export type NotificationID = string;

export type Notification = {
  /**
   * This is used to explicitly define the structure of content.
   */
  readonly type: string;
  readonly when: Date;
  readonly id: NotificationID;
  content: object;
};

export interface NotificationsStore {
  addNotifications(...notifications: Notification[]): Promise<void>;

  getNotifications(): Promise<Notification[]>;

  removeNotifications(ids: NotificationID[]): Promise<void>;
}

/**
 * Implements a NotificationStore that maintains the notifications in memory.
 */
export class TransientNotificationStore implements NotificationsStore {
  constructor(
    private readonly store: Map<NotificationID, Notification> = new Map()
  ) {}

  private addNotification(notification: Notification) {
    this.store.set(notification.id, notification);
    return notification;
  }

  private removeNotification(id: NotificationID) {
    return this.store.delete(id);
  }

  // eslint-disable-next-line @typescript-eslint/require-await
  async addNotifications(...notifications: Notification[]): Promise<void> {
    notifications.forEach((item) => {
      this.addNotification(item);
    });
  }

  // eslint-disable-next-line @typescript-eslint/require-await
  async getNotifications(): Promise<Notification[]> {
    return Array.from(this.store.values());
  }

  // eslint-disable-next-line @typescript-eslint/require-await
  async removeNotifications(ids: NotificationID[]): Promise<void> {
    ids.forEach((item) => {
      this.removeNotification(item);
    });
  }
}
