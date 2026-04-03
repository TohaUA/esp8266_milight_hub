import { schemas } from "@/api";
import React, {
  createContext,
  useContext,
  useEffect,
  useState,
  ReactNode,
} from "react";
import useWebSocket from "react-use-websocket";
import { z } from "zod";

type WebSocketMessage = z.infer<typeof schemas.WebSocketMessage>;

interface WebSocketContextType {
  lastMessage: WebSocketMessage;
  allMessages: WebSocketMessage[];
}

const WebSocketContext = createContext<WebSocketContextType | null>(null);

export const WebSocketProvider: React.FC<{ children: ReactNode }> = ({
  children,
}) => {
  const { lastJsonMessage, sendJsonMessage } = useWebSocket(
    `ws://${window.location.hostname}:81`,
    {
      share: true,
      shouldReconnect: () => true,
      reconnectInterval: 3000,
      reconnectAttempts: Infinity,
    }
  );
  const [messages, setMessages] = useState<WebSocketMessage[]>([]);

  useEffect(() => {
    if (lastJsonMessage !== null) {
      setMessages((prev) => {
        const updated = [...prev, lastJsonMessage as WebSocketMessage];
        return updated.length > 500 ? updated.slice(-500) : updated;
      });
    }
  }, [lastJsonMessage]);

  return (
    <WebSocketContext.Provider
      value={{
        lastMessage: messages[messages.length - 1],
        allMessages: messages,
      }}
    >
      {children}
    </WebSocketContext.Provider>
  );
};

export const useWebSocketContext = () => {
  const context = useContext(WebSocketContext);
  if (!context) {
    throw new Error(
      "useWebSocketContext must be used within a WebSocketProvider"
    );
  }
  return context;
};
