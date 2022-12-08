export type NotificationID = string;

export type Notification = {
  /**
   * This is used to explicitly define the structure of content.
   */
  readonly type: string;
  readonly when: Date;
  readonly id: NotificationID;
  content: any;
};

export interface NotificationsStore {
  addNotifications(...notifications: Notification[]): Promise<void>;

  getNotifications(): Promise<Notification[]>;

  removeNotifications(ids: NotificationID[]): Promise<void>;

  removeNotificationsByType(type: string): Promise<void>;
}
