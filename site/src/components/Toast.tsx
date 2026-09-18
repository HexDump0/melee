/* A one-line status message, used when a destination has no URL yet.

   `ToastProvider` wraps the app; anything under it can call `useToast()`. It
   renders nothing until something is shown, so it costs nothing on the
   prerendered page. */

import { createContext, useCallback, useContext, useEffect, useRef, useState } from 'react';
import type { ReactNode } from 'react';

type ShowToast = (message: string) => void;

const ToastContext = createContext<ShowToast | null>(null);

const VISIBLE_MS = 3600;

export function ToastProvider({ children }: { children: ReactNode }) {
  const [message, setMessage] = useState<string | null>(null);
  const timer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);

  const show = useCallback<ShowToast>((next) => {
    setMessage(next);
    clearTimeout(timer.current);
    timer.current = setTimeout(() => setMessage(null), VISIBLE_MS);
  }, []);

  useEffect(() => () => clearTimeout(timer.current), []);

  return (
    <ToastContext.Provider value={show}>
      {children}
      <div
        role="status"
        aria-live="polite"
        data-show={message ? '' : undefined}
        className="pointer-events-none fixed bottom-6 left-1/2 z-40 -translate-x-1/2 bg-mu-white px-5 py-3 text-micro uppercase text-mu-black opacity-0 shadow-lg transition-opacity duration-300 data-show:opacity-100"
      >
        {message}
      </div>
    </ToastContext.Provider>
  );
}

export function useToast(): ShowToast {
  const show = useContext(ToastContext);
  if (!show) throw new Error('useToast must be used inside <ToastProvider>');
  return show;
}
