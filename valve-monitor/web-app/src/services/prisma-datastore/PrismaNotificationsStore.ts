import { PrismaClient } from "@prisma/client";
import { Notification, NotificationsStore } from "../NotificationsStore";

function toPrismaNotification(n: Notification) {
  const notification = n;
  if (notification.content === null) {
    notification.content = {};
  }
  return notification;
}

export default class PrismaNotificationsStore implements NotificationsStore {
  constructor(private readonly prisma: PrismaClient) {}

  async addNotifications(...notifications: Notification[]): Promise<void> {
    await this.prisma.notification.createMany({
      data: notifications.map((n) => toPrismaNotification(n)),
    });
  }

  async getNotifications(): Promise<Notification[]> {
    return (
      await this.prisma.notification.findMany({
        orderBy: {
          when: "desc",
        },
      })
    ).map((n) => n as Notification);
  }

  async removeNotifications(ids: string[]): Promise<void> {
    await this.prisma.notification.deleteMany({
      where: {
        id: {
          in: ids,
        },
      },
    });
  }
}
