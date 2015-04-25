package com.ullink.ultools.locks;

import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReadWriteLock;
import java.util.concurrent.locks.ReentrantLock;
import java.util.concurrent.locks.ReentrantReadWriteLock;

public class ProfilingReentrantLock
{
    public ProfilingReentrantLock()
    {
        StackTraceElement e = Thread.currentThread().getStackTrace()[2];
        System.out.println(e.getMethodName() + ":" + e.getLineNumber());
    }

    private static void p(Lock lock)
    {
        lock.lock();
        try
        {
            try
            {
                Thread.sleep(100);
            }
            catch (InterruptedException e)
            {
                e.printStackTrace();
            }
        }
        finally
        {
            lock.unlock();
        }

    }

    private static void profReentrantLock()
    {
        final Lock lock = new ReentrantLock();
        new Thread()
        {
            @Override
            public void run()
            {
                while (!isInterrupted())
                {
                    lock.lock();
                    try
                    {
                        try
                        {
                            Thread.sleep(50);
                        }
                        catch (InterruptedException e)
                        {
                            e.printStackTrace();
                        }
                    }
                    finally
                    {
                        lock.unlock();
                    }
                }
            }
        }.start();
        while (true)
            p(lock);
    }

    private static void profReentrantReadWriteLock()
    {
        final ReadWriteLock lock = new ReentrantReadWriteLock();
        final Lock writeLock = lock.writeLock();
        new Thread()
        {
            @Override
            public void run()
            {
                while (!isInterrupted())
                {
                    writeLock.lock();
                    try
                    {
                        try
                        {
                            Thread.sleep(50);
                        }
                        catch (InterruptedException e)
                        {
                            e.printStackTrace();
                        }
                    }
                    finally
                    {
                        writeLock.unlock();
                    }
                }
            }
        }.start();
        while (true)
            p(writeLock);
    }

    public static void main(String[] args)
    {
        System.out.println("profiling...");
        profReentrantReadWriteLock();
    }
}
