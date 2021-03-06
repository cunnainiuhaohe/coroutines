// Copyright (c) 2013 Maciej Gajewski

#ifndef PROFILING_GUI_COROUTINES_MODEL_HPP
#define PROFILING_GUI_COROUTINES_MODEL_HPP

#include "profiling_gui/globals.hpp"

#include <QAbstractListModel>
#include <QColor>
#include <QItemSelectionModel>

namespace profiling_gui {

class CoroutinesModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit CoroutinesModel(QObject *parent = 0);
    
    struct Record
    {
        quintptr id;
        QString name;
        QColor color;
        double timeExecuted;
    };

    void append(const Record& record);
    void clear();

    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        if (!parent.isValid())
            return _records.size();
        else
            return 0;
    }

    virtual int columnCount(const QModelIndex &parent) const override
    {
        if (!parent.isValid())
            return 2;
        else
            return 0;
    }

    virtual QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    virtual void sort(int column, Qt::SortOrder order) override;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    QItemSelectionModel* selectionModel() { return &_selectionModel; }

public slots:

    void onCoroutineSelected(quintptr id);

signals:

    void coroSelected(quintptr id);

private slots:

    void onSelectionChanged(QModelIndex index);

private:

    static QPixmap iconFromColor(QColor color);

    QVector<Record> _records;

    QItemSelectionModel _selectionModel;
};

}

#endif
