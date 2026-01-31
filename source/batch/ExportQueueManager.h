#ifndef EXPORTQUEUEMANAGER_H
#define EXPORTQUEUEMANAGER_H

/**
 * @file ExportQueueManager.h
 * @brief Singleton manager for queued batch export operations.
 * 
 * Part of Phase 3: Launcher UI Integration for batch operations.
 * 
 * Features:
 * - Queues multiple export jobs
 * - Runs exports on background thread (non-blocking UI)
 * - Emits progress signals for UI updates
 * - Supports cancellation
 * - Processes jobs in FIFO order
 * 
 * @see docs/private/BATCH_OPERATIONS.md
 */

#include <QObject>
#include <QQueue>
#include <QMutex>
#include <atomic>

#include "BatchOperations.h"

class QThread;

/**
 * @brief Singleton manager for queued batch export operations.
 * 
 * Manages a queue of export jobs and processes them on a background thread.
 * Signals are emitted for progress updates and completion.
 * 
 * Usage:
 * @code
 * ExportQueueManager* mgr = ExportQueueManager::instance();
 * connect(mgr, &ExportQueueManager::progressChanged, this, &MyWidget::updateProgress);
 * connect(mgr, &ExportQueueManager::jobComplete, this, &MyWidget::showResult);
 * 
 * BatchOps::ExportPdfOptions options;
 * options.outputPath = "/output/dir";
 * options.dpi = 150;
 * mgr->enqueuePdfExport(bundlePaths, options);
 * @endcode
 */
class ExportQueueManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Get the singleton instance.
     */
    static ExportQueueManager* instance();
    
    /**
     * @brief Queue a PDF export job.
     * @param bundles List of .snb bundle paths to export
     * @param options PDF export options
     */
    void enqueuePdfExport(const QStringList& bundles, const BatchOps::ExportPdfOptions& options);
    
    /**
     * @brief Queue an SNBX export job.
     * @param bundles List of .snb bundle paths to export
     * @param options SNBX export options
     */
    void enqueueSnbxExport(const QStringList& bundles, const BatchOps::ExportSnbxOptions& options);
    
    /**
     * @brief Get the number of queued jobs (not including current).
     */
    int queuedJobCount() const;
    
    /**
     * @brief Check if an export is currently in progress.
     */
    bool isExporting() const;
    
    /**
     * @brief Cancel the current export and clear the queue.
     * 
     * The current job will complete its current file before stopping.
     * Already exported files are not affected.
     */
    void cancelAll();

signals:
    /**
     * @brief Emitted when export progress changes.
     * @param currentFile Name of file being exported
     * @param current Current file number (1-based)
     * @param total Total files in current job
     * @param queuedJobs Number of jobs waiting in queue
     */
    void progressChanged(const QString& currentFile, int current, int total, int queuedJobs);
    
    /**
     * @brief Emitted when a job completes (success or error).
     * @param result Batch result with per-file details
     * @param outputDir Output directory path
     */
    void jobComplete(const BatchOps::BatchResult& result, const QString& outputDir);
    
    /**
     * @brief Emitted when the queue becomes empty.
     */
    void queueEmpty();
    
    /**
     * @brief Emitted when an export is cancelled.
     * @param result Partial result (files exported before cancellation)
     */
    void exportCancelled(const BatchOps::BatchResult& result);

private slots:
    void processNextJob();
    void onWorkerProgress(const QString& file, int current, int total);
    void onWorkerComplete(const BatchOps::BatchResult& result, const QString& outputDir);

private:
    explicit ExportQueueManager(QObject* parent = nullptr);
    ~ExportQueueManager();
    
    // Prevent copying
    ExportQueueManager(const ExportQueueManager&) = delete;
    ExportQueueManager& operator=(const ExportQueueManager&) = delete;
    
    void startProcessing();
    
    /**
     * @brief Export job in queue.
     */
    struct ExportJob {
        enum Type { Pdf, Snbx };
        Type type;
        QStringList bundles;
        BatchOps::ExportPdfOptions pdfOptions;
        BatchOps::ExportSnbxOptions snbxOptions;
    };
    
    // Queue and state
    QQueue<ExportJob> m_queue;
    mutable QMutex m_queueMutex;
    std::atomic<bool> m_exporting{false};
    std::atomic<bool> m_cancelled{false};
    
    // Worker thread
    QThread* m_workerThread = nullptr;
    
    // Singleton instance
    static ExportQueueManager* s_instance;
};

// ============================================================================
// Worker object (internal, runs on background thread)
// ============================================================================

/**
 * @brief Worker object that performs exports on background thread.
 * @internal
 */
class ExportWorker : public QObject
{
    Q_OBJECT

public:
    explicit ExportWorker(QObject* parent = nullptr);
    
    void setJob(const QStringList& bundles, 
                const BatchOps::ExportPdfOptions& options,
                std::atomic<bool>* cancelled);
    
    void setJob(const QStringList& bundles,
                const BatchOps::ExportSnbxOptions& options,
                std::atomic<bool>* cancelled);

public slots:
    void process();

signals:
    void progress(const QString& file, int current, int total);
    void complete(const BatchOps::BatchResult& result, const QString& outputDir);

private:
    enum JobType { None, Pdf, Snbx };
    JobType m_jobType = None;
    QStringList m_bundles;
    BatchOps::ExportPdfOptions m_pdfOptions;
    BatchOps::ExportSnbxOptions m_snbxOptions;
    std::atomic<bool>* m_cancelled = nullptr;
};

#endif // EXPORTQUEUEMANAGER_H
