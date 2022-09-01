import { PrismaClient } from "@prisma/client";
import { Notification, NotificationsStore } from "../NotificationsStore";

export default class PrismaNotificationsStore implements NotificationsStore {

    constructor (private readonly prisma: PrismaClient) {}

    async addNotifications(...notifications: Notification[]): Promise<void> {
        await this.prisma.notification.createMany({data: notifications.map(n => this.toPrismaNotification(n))});
    }

    async getNotifications(): Promise<Notification[]> {
        return (await this.prisma.notification.findMany({
            orderBy: {
               when: "desc"
            }
        })).map(n => n as Notification);
    }

    async removeNotifications(ids: string[]): Promise<void> {
        await this.prisma.notification.deleteMany({
            where: {
                id: {
                    in: ids
                }
            }
        });
    }


    private toPrismaNotification(n: Notification) {
        if (n.content===null) {
            n.content = {};
        }
        return n;
    }

}